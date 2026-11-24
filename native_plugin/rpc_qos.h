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

        // DST writes vanilla 7-bit RPC ID as first aligned byte in BitStream.
        const uint8_t rpcId = (bsData != nullptr && bsBits >= 8u)
            ? static_cast<uint8_t>(bsData[0] & 0x7Fu)
            : 0u;

        Classification res{ origPriority, origChannel, false, false };

        if (std::strcmp(uniqueID, "RPC") == 0) {
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
        } else if (std::strcmp(uniqueID, "ModRPC") == 0) {
            res.orderingChannel = 0u;
            if (bsBits <= kSmallModRpcMaxBits) {
                res.priority          = kImmediatePriority;
                res.promotedImmediate = (origPriority != kImmediatePriority);
            }
        } else if (std::strcmp(uniqueID, "ClientRPC") == 0) {
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
        } else if (std::strcmp(uniqueID, "ClientModRPC") == 0) {
            res.orderingChannel = 0u;
            if (bsBits <= kSmallModRpcMaxBits) {
                res.priority          = kImmediatePriority;
                res.promotedImmediate = (origPriority != kImmediatePriority);
            }
        } else if (std::strcmp(uniqueID, "ShardRPC") == 0 ||
                   std::strcmp(uniqueID, "ShardModRPC") == 0) {
            res.orderingChannel = origChannel;
            if (bsBits <= kSmallModRpcMaxBits) {
                res.priority          = kImmediatePriority;
                res.promotedImmediate = (origPriority != kImmediatePriority);
            }
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
