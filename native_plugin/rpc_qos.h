#pragma once
// =============================================================================
// HyperSmooth / HyperCore Causal-Safe Network RPC QoS Classifier
// =============================================================================
// Classifies RakNet::RPC4::Call() traffic into:
//   - Channel 0 / IMMEDIATE_PRIORITY (0): Combat, movement, equip, inventory,
//     action, small <=256B ModRPC/ClientModRPC, and entity-bearing ClientRPC
//     (e.g. TryToTeachScrapbookData) -> strictly FIFO-ordered with gameplay!
//   - Channel 1 / HIGH_PRIORITY (1): Non-combat Client->Server social/UI/popup
//     RPCs (bitmask populated dynamically at runtime from Lua GLOBAL.RPC).
//   - Channel 2 / MEDIUM_PRIORITY (2): Entity-free Server->Client broadcast
//     ClientRPCs (chat history, recipes, plant/food knowledge, accomplishments,
//     popups) -> isolated from ReplicaManager3 entity/combat replication!
// Fail-closed: Only rewrites RELIABLE_ORDERED (3) packets targeting Ch0.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>

namespace hs_rpc_qos {

struct RpcBitmask {
    std::atomic<uint64_t> lo{0};
    std::atomic<uint64_t> hi{0};

    void SetMask(uint64_t newLo, uint64_t newHi) noexcept {
        lo.store(newLo, std::memory_order_release);
        hi.store(newHi, std::memory_order_release);
    }

    bool Test(uint8_t rpcId) const noexcept {
        if (rpcId == 0u || rpcId >= 128u) return false;
        if (rpcId < 64u) {
            return (lo.load(std::memory_order_acquire) & (1ULL << rpcId)) != 0ULL;
        }
        return (hi.load(std::memory_order_acquire) & (1ULL << (rpcId - 64u))) != 0ULL;
    }
};

struct Classification {
    int     priority;
    uint8_t orderingChannel;
    bool    promotedImmediate;
    bool    movedOffCh0;
};

class Classifier {
public:
    static constexpr int      kImmediatePriority = 0;
    static constexpr int      kHighPriority      = 1;
    static constexpr int      kMediumPriority    = 2;
    static constexpr int      kReliableOrdered   = 3;
    static constexpr uint32_t kSmallModRpcMaxBits = 2048u; // 256 bytes

    void SetMasks(uint64_t srvLo, uint64_t srvHi, uint64_t cliLo, uint64_t cliHi) noexcept {
        serverNonCombatMask_.SetMask(srvLo, srvHi);
        clientNonEntityMask_.SetMask(cliLo, cliHi);
    }

    Classification Classify(const char* uniqueID,
                            const uint8_t* bsData,
                            uint32_t bsBits,
                            int origPriority,
                            int reliability,
                            uint8_t origChannel) noexcept {
        totalCalls_.fetch_add(1, std::memory_order_relaxed);

        // Fail-closed: preserve non-RELIABLE_ORDERED traffic and explicit non-Ch0 callers.
        if (!uniqueID || reliability != kReliableOrdered || origChannel != 0u) {
            return Classification{ origPriority, origChannel, false, false };
        }

        // DST registers all 41 RakNet::RPC4 slots with single-character ASCII uniqueIDs:
        //   "0" = HandleRPC          (Client -> Server vanilla RPC)
        //   "1" = HandleModRPC       (Client -> Server ModRPC)
        //   "2" = HandleClientRPC    (Server -> Client vanilla ClientRPC)
        //   "3" = HandleClientModRPC (Server -> Client ClientModRPC)
        //   "4" = HandleShardRPC     (Shard  <-> Shard vanilla ShardRPC)
        //   "5" = HandleShardModRPC  (Shard  <-> Shard ShardModRPC)
        //   "6".."X" = Klei native C++ engine RPCs (chat/vote/spawn/resync) -> fail-closed passthrough!
        // Symbolic aliases ("RPC".."ShardModRPC") are retained for readability/tests.
        enum class SlotKind : uint8_t {
            Unknown = 0,
            ServerRpc,
            ServerModRpc,
            ClientRpc,
            ClientModRpc,
            ShardRpc,
        };

        SlotKind kind = SlotKind::Unknown;
        if (uniqueID[0] != '\0' && uniqueID[1] == '\0') {
            switch (uniqueID[0]) {
                case '0': kind = SlotKind::ServerRpc;    break;
                case '1': kind = SlotKind::ServerModRpc; break;
                case '2': kind = SlotKind::ClientRpc;    break;
                case '3': kind = SlotKind::ClientModRpc; break;
                case '4':
                case '5': kind = SlotKind::ShardRpc;     break;
                default:  kind = SlotKind::Unknown;      break;
            }
        } else if (std::strcmp(uniqueID, "RPC") == 0) {
            kind = SlotKind::ServerRpc;
        } else if (std::strcmp(uniqueID, "ModRPC") == 0) {
            kind = SlotKind::ServerModRpc;
        } else if (std::strcmp(uniqueID, "ClientRPC") == 0) {
            kind = SlotKind::ClientRpc;
        } else if (std::strcmp(uniqueID, "ClientModRPC") == 0) {
            kind = SlotKind::ClientModRpc;
        } else if (std::strcmp(uniqueID, "ShardRPC") == 0 ||
                   std::strcmp(uniqueID, "ShardModRPC") == 0) {
            kind = SlotKind::ShardRpc;
        }

        // DST BitStream wire layout for Slot "0" (HandleRPC) and Slot "2" (HandleClientRPC):
        //   bit 0 (MSB, 0x80) = 1-bit boolean tick flag
        //   bits 1..7 (0x7F)  = 7-bit 1-based Lua RPC ID (aligned right via BitStream::ReadBits(7, true))
        const uint8_t rpcId = (bsData != nullptr && bsBits >= 8u)
            ? static_cast<uint8_t>(bsData[0] & 0x7Fu)
            : 0u;

        Classification res{ origPriority, origChannel, false, false };

        switch (kind) {
            case SlotKind::ServerRpc:
                if (serverNonCombatMask_.Test(rpcId)) {
                    res.priority          = kHighPriority;
                    res.orderingChannel   = 1u;
                    res.promotedImmediate = false;
                    res.movedOffCh0       = true;
                } else {
                    res.priority          = kImmediatePriority;
                    res.orderingChannel   = 0u;
                    res.promotedImmediate = (origPriority != kImmediatePriority);
                    res.movedOffCh0       = false;
                }
                break;

            case SlotKind::ServerModRpc:
                res.orderingChannel = 0u;
                if (bsBits <= kSmallModRpcMaxBits) {
                    res.priority          = kImmediatePriority;
                    res.promotedImmediate = (origPriority != kImmediatePriority);
                }
                break;

            case SlotKind::ClientRpc:
                if (clientNonEntityMask_.Test(rpcId)) {
                    res.priority          = kMediumPriority;
                    res.orderingChannel   = 2u;
                    res.promotedImmediate = false;
                    res.movedOffCh0       = true;
                } else {
                    res.priority          = kImmediatePriority;
                    res.orderingChannel   = 0u;
                    res.promotedImmediate = (origPriority != kImmediatePriority);
                    res.movedOffCh0       = false;
                }
                break;

            case SlotKind::ClientModRpc:
                res.orderingChannel = 0u;
                if (bsBits <= kSmallModRpcMaxBits) {
                    res.priority          = kImmediatePriority;
                    res.promotedImmediate = (origPriority != kImmediatePriority);
                }
                break;

            case SlotKind::ShardRpc:
                res.orderingChannel = origChannel;
                if (bsBits <= kSmallModRpcMaxBits) {
                    res.priority          = kImmediatePriority;
                    res.promotedImmediate = (origPriority != kImmediatePriority);
                }
                break;

            case SlotKind::Unknown:
            default:
                break;
        }

        if (res.promotedImmediate) {
            immCount_.fetch_add(1, std::memory_order_relaxed);
        }
        if (res.movedOffCh0) {
            offCh0Count_.fetch_add(1, std::memory_order_relaxed);
        }
        return res;
    }

    int64_t ImmCount() const noexcept { return immCount_.load(std::memory_order_relaxed); }
    int64_t OffCh0Count() const noexcept { return offCh0Count_.load(std::memory_order_relaxed); }
    int64_t TotalCalls() const noexcept { return totalCalls_.load(std::memory_order_relaxed); }

private:
    RpcBitmask           serverNonCombatMask_{};
    RpcBitmask           clientNonEntityMask_{};
    std::atomic<int64_t> immCount_{0};
    std::atomic<int64_t> offCh0Count_{0};
    std::atomic<int64_t> totalCalls_{0};
};

} // namespace hs_rpc_qos
