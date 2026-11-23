#pragma once
#include <cstddef>
#include <cstdint>

static constexpr size_t kIphlpExportCount = 297;
static const char* const kIphlpExportNames[kIphlpExportCount] = {
    "AddIPAddress",
    "AllocateAndGetInterfaceInfoFromStack",
    "AllocateAndGetIpAddrTableFromStack",
    "CancelIPChangeNotify",
    "CancelIfTimestampConfigChange",
    "CancelMibChangeNotify2",
    "CaptureInterfaceHardwareCrossTimestamp",
    "CloseCompartment",
    "CloseGetIPPhysicalInterfaceForDestination",
    "ConvertCompartmentGuidToId",
    "ConvertCompartmentIdToGuid",
    "ConvertGuidToStringA",
    "ConvertGuidToStringW",
    "ConvertInterfaceAliasToLuid",
    "ConvertInterfaceGuidToLuid",
    "ConvertInterfaceIndexToLuid",
    "ConvertInterfaceLuidToAlias",
    "ConvertInterfaceLuidToGuid",
    "ConvertInterfaceLuidToIndex",
    "ConvertInterfaceLuidToNameA",
    "ConvertInterfaceLuidToNameW",
    "ConvertInterfaceNameToLuidA",
    "ConvertInterfaceNameToLuidW",
    "ConvertInterfacePhysicalAddressToLuid",
    "ConvertIpv4MaskToLength",
    "ConvertLengthToIpv4Mask",
    "ConvertRemoteInterfaceAliasToLuid",
    "ConvertRemoteInterfaceGuidToLuid",
    "ConvertRemoteInterfaceIndexToLuid",
    "ConvertRemoteInterfaceLuidToAlias",
    "ConvertRemoteInterfaceLuidToGuid",
    "ConvertRemoteInterfaceLuidToIndex",
    "ConvertStringToGuidA",
    "ConvertStringToGuidW",
    "ConvertStringToInterfacePhysicalAddress",
    "CreateAnycastIpAddressEntry",
    "CreateCompartment",
    "CreateIpForwardEntry",
    "CreateIpForwardEntry2",
    "CreateIpNetEntry",
    "CreateIpNetEntry2",
    "CreatePersistentTcpPortReservation",
    "CreatePersistentUdpPortReservation",
    "CreateProxyArpEntry",
    "CreateSortedAddressPairs",
    "CreateUnicastIpAddressEntry",
    "DeleteAnycastIpAddressEntry",
    "DeleteCompartment",
    "DeleteIPAddress",
    "DeleteIpForwardEntry",
    "DeleteIpForwardEntry2",
    "DeleteIpNetEntry",
    "DeleteIpNetEntry2",
    "DeletePersistentTcpPortReservation",
    "DeletePersistentUdpPortReservation",
    "DeleteProxyArpEntry",
    "DeleteUnicastIpAddressEntry",
    "DisableMediaSense",
    "EnableRouter",
    "FlushIpNetTable",
    "FlushIpNetTable2",
    "FlushIpPathTable",
    "FreeDnsSettings",
    "FreeInterfaceDnsSettings",
    "FreeMibTable",
    "GetAdapterIndex",
    "GetAdapterOrderMap",
    "GetAdaptersAddresses",
    "GetAdaptersInfo",
    "GetAnycastIpAddressEntry",
    "GetAnycastIpAddressTable",
    "GetBestInterface",
    "GetBestInterfaceEx",
    "GetBestRoute",
    "GetBestRoute2",
    "GetCurrentThreadCompartmentId",
    "GetCurrentThreadCompartmentScope",
    "GetDefaultCompartmentId",
    "GetDnsSettings",
    "GetExtendedTcpTable",
    "GetExtendedUdpTable",
    "GetFriendlyIfIndex",
    "GetIcmpStatistics",
    "GetIcmpStatisticsEx",
    "GetIfEntry",
    "GetIfEntry2",
    "GetIfEntry2Ex",
    "GetIfStackTable",
    "GetIfTable",
    "GetIfTable2",
    "GetIfTable2Ex",
    "GetInterfaceCompartmentId",
    "GetInterfaceCurrentTimestampCapabilities",
    "GetInterfaceDnsSettings",
    "GetInterfaceHardwareTimestampCapabilities",
    "GetInterfaceInfo",
    "GetInvertedIfStackTable",
    "GetIpAddrTable",
    "GetIpErrorString",
    "GetIpForwardEntry2",
    "GetIpForwardTable",
    "GetIpForwardTable2",
    "GetIpInterfaceEntry",
    "GetIpInterfaceTable",
    "GetIpNetEntry2",
    "GetIpNetTable",
    "GetIpNetTable2",
    "GetIpNetworkConnectionBandwidthEstimates",
    "GetIpPathEntry",
    "GetIpPathTable",
    "GetIpStatistics",
    "GetIpStatisticsEx",
    "GetJobCompartmentId",
    "GetMulticastIpAddressEntry",
    "GetMulticastIpAddressTable",
    "GetNetworkConnectivityHint",
    "GetNetworkConnectivityHintForInterface",
    "GetNetworkInformation",
    "GetNetworkParams",
    "GetNumberOfInterfaces",
    "GetOwnerModuleFromPidAndInfo",
    "GetOwnerModuleFromTcp6Entry",
    "GetOwnerModuleFromTcpEntry",
    "GetOwnerModuleFromUdp6Entry",
    "GetOwnerModuleFromUdpEntry",
    "GetPerAdapterInfo",
    "GetPerTcp6ConnectionEStats",
    "GetPerTcp6ConnectionStats",
    "GetPerTcpConnectionEStats",
    "GetPerTcpConnectionStats",
    "GetRTTAndHopCount",
    "GetSessionCompartmentId",
    "GetTcp6Table",
    "GetTcp6Table2",
    "GetTcpStatistics",
    "GetTcpStatisticsEx",
    "GetTcpStatisticsEx2",
    "GetTcpTable",
    "GetTcpTable2",
    "GetTeredoPort",
    "GetUdp6Table",
    "GetUdpStatistics",
    "GetUdpStatisticsEx",
    "GetUdpStatisticsEx2",
    "GetUdpTable",
    "GetUniDirectionalAdapterInfo",
    "GetUnicastIpAddressEntry",
    "GetUnicastIpAddressTable",
    "GetWPAOACSupportLevel",
    "Icmp6CreateFile",
    "Icmp6ParseReplies",
    "Icmp6SendEcho2",
    "IcmpCloseHandle",
    "IcmpCreateFile",
    "IcmpParseReplies",
    "IcmpSendEcho",
    "IcmpSendEcho2",
    "IcmpSendEcho2Ex",
    "InitializeCompartmentEntry",
    "InitializeIpForwardEntry",
    "InitializeIpInterfaceEntry",
    "InitializeUnicastIpAddressEntry",
    "InternalCleanupPersistentStore",
    "InternalCreateAnycastIpAddressEntry",
    "InternalCreateIpForwardEntry",
    "InternalCreateIpForwardEntry2",
    "InternalCreateIpNetEntry",
    "InternalCreateIpNetEntry2",
    "InternalCreateOrRefIpForwardEntry2",
    "InternalCreateUnicastIpAddressEntry",
    "InternalDeleteAnycastIpAddressEntry",
    "InternalDeleteIpForwardEntry",
    "InternalDeleteIpForwardEntry2",
    "InternalDeleteIpNetEntry",
    "InternalDeleteIpNetEntry2",
    "InternalDeleteUnicastIpAddressEntry",
    "InternalFindInterfaceByAddress",
    "InternalGetAnycastIpAddressEntry",
    "InternalGetAnycastIpAddressTable",
    "InternalGetBoundTcp6EndpointTable",
    "InternalGetBoundTcpEndpointTable",
    "InternalGetForwardIpTable2",
    "InternalGetIPPhysicalInterfaceForDestination",
    "InternalGetIfEntry2",
    "InternalGetIfTable",
    "InternalGetIfTable2",
    "InternalGetIpAddrTable",
    "InternalGetIpForwardEntry2",
    "InternalGetIpForwardTable",
    "InternalGetIpInterfaceEntry",
    "InternalGetIpInterfaceTable",
    "InternalGetIpNetEntry2",
    "InternalGetIpNetTable",
    "InternalGetIpNetTable2",
    "InternalGetMulticastIpAddressEntry",
    "InternalGetMulticastIpAddressTable",
    "InternalGetRtcSlotInformation",
    "InternalGetTcp6Table2",
    "InternalGetTcp6TableWithOwnerModule",
    "InternalGetTcp6TableWithOwnerPid",
    "InternalGetTcpTable",
    "InternalGetTcpTable2",
    "InternalGetTcpTableEx",
    "InternalGetTcpTableWithOwnerModule",
    "InternalGetTcpTableWithOwnerPid",
    "InternalGetTunnelPhysicalAdapter",
    "InternalGetUdp6TableWithOwnerModule",
    "InternalGetUdp6TableWithOwnerPid",
    "InternalGetUdpTable",
    "InternalGetUdpTableEx",
    "InternalGetUdpTableWithOwnerModule",
    "InternalGetUdpTableWithOwnerPid",
    "InternalGetUnicastIpAddressEntry",
    "InternalGetUnicastIpAddressTable",
    "InternalIcmpCreateFileEx",
    "InternalSetIfEntry",
    "InternalSetIpForwardEntry",
    "InternalSetIpForwardEntry2",
    "InternalSetIpInterfaceEntry",
    "InternalSetIpNetEntry",
    "InternalSetIpNetEntry2",
    "InternalSetIpStats",
    "InternalSetTcpEntry",
    "InternalSetTeredoPort",
    "InternalSetUnicastIpAddressEntry",
    "IpReleaseAddress",
    "IpRenewAddress",
    "LookupPersistentTcpPortReservation",
    "LookupPersistentUdpPortReservation",
    "NTPTimeToNTFileTime",
    "NTTimeToNTPTime",
    "NhGetGuidFromInterfaceName",
    "NhGetInterfaceDescriptionFromGuid",
    "NhGetInterfaceNameFromDeviceGuid",
    "NhGetInterfaceNameFromGuid",
    "NhpAllocateAndGetInterfaceInfoFromStack",
    "NotifyAddrChange",
    "NotifyCompartmentChange",
    "NotifyIfTimestampConfigChange",
    "NotifyIpInterfaceChange",
    "NotifyNetworkConnectivityHintChange",
    "NotifyRouteChange",
    "NotifyRouteChange2",
    "NotifyStableUnicastIpAddressTable",
    "NotifyTeredoPortChange",
    "NotifyUnicastIpAddressChange",
    "OpenCompartment",
    "ParseNetworkString",
    "PfAddFiltersToInterface",
    "PfAddGlobalFilterToInterface",
    "PfBindInterfaceToIPAddress",
    "PfBindInterfaceToIndex",
    "PfCreateInterface",
    "PfDeleteInterface",
    "PfDeleteLog",
    "PfGetInterfaceStatistics",
    "PfMakeLog",
    "PfRebindFilters",
    "PfRemoveFilterHandles",
    "PfRemoveFiltersFromInterface",
    "PfRemoveGlobalFilterFromInterface",
    "PfSetLogBuffer",
    "PfTestPacket",
    "PfUnBindInterface",
    "ResolveIpNetEntry2",
    "ResolveNeighbor",
    "RestoreMediaSense",
    "SendARP",
    "SetAdapterIpAddress",
    "SetCurrentThreadCompartmentId",
    "SetCurrentThreadCompartmentScope",
    "SetDnsSettings",
    "SetIfEntry",
    "SetInterfaceDnsSettings",
    "SetIpForwardEntry",
    "SetIpForwardEntry2",
    "SetIpInterfaceEntry",
    "SetIpNetEntry",
    "SetIpNetEntry2",
    "SetIpStatistics",
    "SetIpStatisticsEx",
    "SetIpTTL",
    "SetJobCompartmentId",
    "SetNetworkInformation",
    "SetPerTcp6ConnectionEStats",
    "SetPerTcp6ConnectionStats",
    "SetPerTcpConnectionEStats",
    "SetPerTcpConnectionStats",
    "SetSessionCompartmentId",
    "SetTcpEntry",
    "SetUnicastIpAddressEntry",
    "UnenableRouter",
    "do_echo_rep",
    "do_echo_req",
    "if_indextoname",
    "if_nametoindex",
    "register_icmp",
};

extern "C" const uint64_t g_iphlp_fail_ret[kIphlpExportCount] = {
    0x7FULL, // AddIPAddress
    0x0ULL, // AllocateAndGetInterfaceInfoFromStack
    0x0ULL, // AllocateAndGetIpAddrTableFromStack
    0x7FULL, // CancelIPChangeNotify
    0x7FULL, // CancelIfTimestampConfigChange
    0x7FULL, // CancelMibChangeNotify2
    0x7FULL, // CaptureInterfaceHardwareCrossTimestamp
    0x7FULL, // CloseCompartment
    0x7FULL, // CloseGetIPPhysicalInterfaceForDestination
    0x7FULL, // ConvertCompartmentGuidToId
    0x7FULL, // ConvertCompartmentIdToGuid
    0x7FULL, // ConvertGuidToStringA
    0x7FULL, // ConvertGuidToStringW
    0x7FULL, // ConvertInterfaceAliasToLuid
    0x7FULL, // ConvertInterfaceGuidToLuid
    0x7FULL, // ConvertInterfaceIndexToLuid
    0x7FULL, // ConvertInterfaceLuidToAlias
    0x7FULL, // ConvertInterfaceLuidToGuid
    0x7FULL, // ConvertInterfaceLuidToIndex
    0x7FULL, // ConvertInterfaceLuidToNameA
    0x7FULL, // ConvertInterfaceLuidToNameW
    0x7FULL, // ConvertInterfaceNameToLuidA
    0x7FULL, // ConvertInterfaceNameToLuidW
    0x7FULL, // ConvertInterfacePhysicalAddressToLuid
    0x7FULL, // ConvertIpv4MaskToLength
    0x7FULL, // ConvertLengthToIpv4Mask
    0x7FULL, // ConvertRemoteInterfaceAliasToLuid
    0x7FULL, // ConvertRemoteInterfaceGuidToLuid
    0x7FULL, // ConvertRemoteInterfaceIndexToLuid
    0x7FULL, // ConvertRemoteInterfaceLuidToAlias
    0x7FULL, // ConvertRemoteInterfaceLuidToGuid
    0x7FULL, // ConvertRemoteInterfaceLuidToIndex
    0x7FULL, // ConvertStringToGuidA
    0x7FULL, // ConvertStringToGuidW
    0x7FULL, // ConvertStringToInterfacePhysicalAddress
    0x7FULL, // CreateAnycastIpAddressEntry
    0x7FULL, // CreateCompartment
    0x7FULL, // CreateIpForwardEntry
    0x7FULL, // CreateIpForwardEntry2
    0x7FULL, // CreateIpNetEntry
    0x7FULL, // CreateIpNetEntry2
    0x7FULL, // CreatePersistentTcpPortReservation
    0x7FULL, // CreatePersistentUdpPortReservation
    0x7FULL, // CreateProxyArpEntry
    0x7FULL, // CreateSortedAddressPairs
    0x7FULL, // CreateUnicastIpAddressEntry
    0x7FULL, // DeleteAnycastIpAddressEntry
    0x7FULL, // DeleteCompartment
    0x7FULL, // DeleteIPAddress
    0x7FULL, // DeleteIpForwardEntry
    0x7FULL, // DeleteIpForwardEntry2
    0x7FULL, // DeleteIpNetEntry
    0x7FULL, // DeleteIpNetEntry2
    0x7FULL, // DeletePersistentTcpPortReservation
    0x7FULL, // DeletePersistentUdpPortReservation
    0x7FULL, // DeleteProxyArpEntry
    0x7FULL, // DeleteUnicastIpAddressEntry
    0x7FULL, // DisableMediaSense
    0x7FULL, // EnableRouter
    0x7FULL, // FlushIpNetTable
    0x7FULL, // FlushIpNetTable2
    0x7FULL, // FlushIpPathTable
    0x0ULL, // FreeDnsSettings
    0x0ULL, // FreeInterfaceDnsSettings
    0x0ULL, // FreeMibTable
    0x7FULL, // GetAdapterIndex
    0x7FULL, // GetAdapterOrderMap
    0x7FULL, // GetAdaptersAddresses
    0x7FULL, // GetAdaptersInfo
    0x7FULL, // GetAnycastIpAddressEntry
    0x7FULL, // GetAnycastIpAddressTable
    0x7FULL, // GetBestInterface
    0x7FULL, // GetBestInterfaceEx
    0x7FULL, // GetBestRoute
    0x7FULL, // GetBestRoute2
    0x7FULL, // GetCurrentThreadCompartmentId
    0x7FULL, // GetCurrentThreadCompartmentScope
    0x7FULL, // GetDefaultCompartmentId
    0x7FULL, // GetDnsSettings
    0x7FULL, // GetExtendedTcpTable
    0x7FULL, // GetExtendedUdpTable
    0x7FULL, // GetFriendlyIfIndex
    0x7FULL, // GetIcmpStatistics
    0x7FULL, // GetIcmpStatisticsEx
    0x7FULL, // GetIfEntry
    0x7FULL, // GetIfEntry2
    0x7FULL, // GetIfEntry2Ex
    0x7FULL, // GetIfStackTable
    0x7FULL, // GetIfTable
    0x7FULL, // GetIfTable2
    0x7FULL, // GetIfTable2Ex
    0x7FULL, // GetInterfaceCompartmentId
    0x7FULL, // GetInterfaceCurrentTimestampCapabilities
    0x7FULL, // GetInterfaceDnsSettings
    0x7FULL, // GetInterfaceHardwareTimestampCapabilities
    0x7FULL, // GetInterfaceInfo
    0x7FULL, // GetInvertedIfStackTable
    0x7FULL, // GetIpAddrTable
    0x7FULL, // GetIpErrorString
    0x7FULL, // GetIpForwardEntry2
    0x7FULL, // GetIpForwardTable
    0x7FULL, // GetIpForwardTable2
    0x7FULL, // GetIpInterfaceEntry
    0x7FULL, // GetIpInterfaceTable
    0x7FULL, // GetIpNetEntry2
    0x7FULL, // GetIpNetTable
    0x7FULL, // GetIpNetTable2
    0x7FULL, // GetIpNetworkConnectionBandwidthEstimates
    0x7FULL, // GetIpPathEntry
    0x7FULL, // GetIpPathTable
    0x7FULL, // GetIpStatistics
    0x7FULL, // GetIpStatisticsEx
    0x7FULL, // GetJobCompartmentId
    0x7FULL, // GetMulticastIpAddressEntry
    0x7FULL, // GetMulticastIpAddressTable
    0x7FULL, // GetNetworkConnectivityHint
    0x7FULL, // GetNetworkConnectivityHintForInterface
    0x7FULL, // GetNetworkInformation
    0x7FULL, // GetNetworkParams
    0x7FULL, // GetNumberOfInterfaces
    0x7FULL, // GetOwnerModuleFromPidAndInfo
    0x7FULL, // GetOwnerModuleFromTcp6Entry
    0x7FULL, // GetOwnerModuleFromTcpEntry
    0x7FULL, // GetOwnerModuleFromUdp6Entry
    0x7FULL, // GetOwnerModuleFromUdpEntry
    0x7FULL, // GetPerAdapterInfo
    0x7FULL, // GetPerTcp6ConnectionEStats
    0x7FULL, // GetPerTcp6ConnectionStats
    0x7FULL, // GetPerTcpConnectionEStats
    0x7FULL, // GetPerTcpConnectionStats
    0x7FULL, // GetRTTAndHopCount
    0x7FULL, // GetSessionCompartmentId
    0x7FULL, // GetTcp6Table
    0x7FULL, // GetTcp6Table2
    0x7FULL, // GetTcpStatistics
    0x7FULL, // GetTcpStatisticsEx
    0x7FULL, // GetTcpStatisticsEx2
    0x7FULL, // GetTcpTable
    0x7FULL, // GetTcpTable2
    0x7FULL, // GetTeredoPort
    0x7FULL, // GetUdp6Table
    0x7FULL, // GetUdpStatistics
    0x7FULL, // GetUdpStatisticsEx
    0x7FULL, // GetUdpStatisticsEx2
    0x7FULL, // GetUdpTable
    0x7FULL, // GetUniDirectionalAdapterInfo
    0x7FULL, // GetUnicastIpAddressEntry
    0x7FULL, // GetUnicastIpAddressTable
    0x7FULL, // GetWPAOACSupportLevel
    0xFFFFFFFFFFFFFFFFULL, // Icmp6CreateFile
    0x7FULL, // Icmp6ParseReplies
    0x7FULL, // Icmp6SendEcho2
    0x0ULL, // IcmpCloseHandle
    0xFFFFFFFFFFFFFFFFULL, // IcmpCreateFile
    0x7FULL, // IcmpParseReplies
    0x7FULL, // IcmpSendEcho
    0x7FULL, // IcmpSendEcho2
    0x7FULL, // IcmpSendEcho2Ex
    0x7FULL, // InitializeCompartmentEntry
    0x7FULL, // InitializeIpForwardEntry
    0x7FULL, // InitializeIpInterfaceEntry
    0x7FULL, // InitializeUnicastIpAddressEntry
    0x7FULL, // InternalCleanupPersistentStore
    0x7FULL, // InternalCreateAnycastIpAddressEntry
    0x7FULL, // InternalCreateIpForwardEntry
    0x7FULL, // InternalCreateIpForwardEntry2
    0x7FULL, // InternalCreateIpNetEntry
    0x7FULL, // InternalCreateIpNetEntry2
    0x7FULL, // InternalCreateOrRefIpForwardEntry2
    0x7FULL, // InternalCreateUnicastIpAddressEntry
    0x7FULL, // InternalDeleteAnycastIpAddressEntry
    0x7FULL, // InternalDeleteIpForwardEntry
    0x7FULL, // InternalDeleteIpForwardEntry2
    0x7FULL, // InternalDeleteIpNetEntry
    0x7FULL, // InternalDeleteIpNetEntry2
    0x7FULL, // InternalDeleteUnicastIpAddressEntry
    0x7FULL, // InternalFindInterfaceByAddress
    0x7FULL, // InternalGetAnycastIpAddressEntry
    0x7FULL, // InternalGetAnycastIpAddressTable
    0x7FULL, // InternalGetBoundTcp6EndpointTable
    0x7FULL, // InternalGetBoundTcpEndpointTable
    0x7FULL, // InternalGetForwardIpTable2
    0x7FULL, // InternalGetIPPhysicalInterfaceForDestination
    0x7FULL, // InternalGetIfEntry2
    0x7FULL, // InternalGetIfTable
    0x7FULL, // InternalGetIfTable2
    0x7FULL, // InternalGetIpAddrTable
    0x7FULL, // InternalGetIpForwardEntry2
    0x7FULL, // InternalGetIpForwardTable
    0x7FULL, // InternalGetIpInterfaceEntry
    0x7FULL, // InternalGetIpInterfaceTable
    0x7FULL, // InternalGetIpNetEntry2
    0x7FULL, // InternalGetIpNetTable
    0x7FULL, // InternalGetIpNetTable2
    0x7FULL, // InternalGetMulticastIpAddressEntry
    0x7FULL, // InternalGetMulticastIpAddressTable
    0x7FULL, // InternalGetRtcSlotInformation
    0x7FULL, // InternalGetTcp6Table2
    0x7FULL, // InternalGetTcp6TableWithOwnerModule
    0x7FULL, // InternalGetTcp6TableWithOwnerPid
    0x7FULL, // InternalGetTcpTable
    0x7FULL, // InternalGetTcpTable2
    0x7FULL, // InternalGetTcpTableEx
    0x7FULL, // InternalGetTcpTableWithOwnerModule
    0x7FULL, // InternalGetTcpTableWithOwnerPid
    0x7FULL, // InternalGetTunnelPhysicalAdapter
    0x7FULL, // InternalGetUdp6TableWithOwnerModule
    0x7FULL, // InternalGetUdp6TableWithOwnerPid
    0x7FULL, // InternalGetUdpTable
    0x7FULL, // InternalGetUdpTableEx
    0x7FULL, // InternalGetUdpTableWithOwnerModule
    0x7FULL, // InternalGetUdpTableWithOwnerPid
    0x7FULL, // InternalGetUnicastIpAddressEntry
    0x7FULL, // InternalGetUnicastIpAddressTable
    0x7FULL, // InternalIcmpCreateFileEx
    0x7FULL, // InternalSetIfEntry
    0x7FULL, // InternalSetIpForwardEntry
    0x7FULL, // InternalSetIpForwardEntry2
    0x7FULL, // InternalSetIpInterfaceEntry
    0x7FULL, // InternalSetIpNetEntry
    0x7FULL, // InternalSetIpNetEntry2
    0x7FULL, // InternalSetIpStats
    0x7FULL, // InternalSetTcpEntry
    0x7FULL, // InternalSetTeredoPort
    0x7FULL, // InternalSetUnicastIpAddressEntry
    0x7FULL, // IpReleaseAddress
    0x7FULL, // IpRenewAddress
    0x7FULL, // LookupPersistentTcpPortReservation
    0x7FULL, // LookupPersistentUdpPortReservation
    0x7FULL, // NTPTimeToNTFileTime
    0x7FULL, // NTTimeToNTPTime
    0x7FULL, // NhGetGuidFromInterfaceName
    0x7FULL, // NhGetInterfaceDescriptionFromGuid
    0x7FULL, // NhGetInterfaceNameFromDeviceGuid
    0x7FULL, // NhGetInterfaceNameFromGuid
    0x7FULL, // NhpAllocateAndGetInterfaceInfoFromStack
    0x7FULL, // NotifyAddrChange
    0x7FULL, // NotifyCompartmentChange
    0x7FULL, // NotifyIfTimestampConfigChange
    0x7FULL, // NotifyIpInterfaceChange
    0x7FULL, // NotifyNetworkConnectivityHintChange
    0x7FULL, // NotifyRouteChange
    0x7FULL, // NotifyRouteChange2
    0x7FULL, // NotifyStableUnicastIpAddressTable
    0x7FULL, // NotifyTeredoPortChange
    0x7FULL, // NotifyUnicastIpAddressChange
    0x7FULL, // OpenCompartment
    0x7FULL, // ParseNetworkString
    0x7FULL, // PfAddFiltersToInterface
    0x7FULL, // PfAddGlobalFilterToInterface
    0x7FULL, // PfBindInterfaceToIPAddress
    0x7FULL, // PfBindInterfaceToIndex
    0x7FULL, // PfCreateInterface
    0x7FULL, // PfDeleteInterface
    0x7FULL, // PfDeleteLog
    0x7FULL, // PfGetInterfaceStatistics
    0x7FULL, // PfMakeLog
    0x7FULL, // PfRebindFilters
    0x7FULL, // PfRemoveFilterHandles
    0x7FULL, // PfRemoveFiltersFromInterface
    0x7FULL, // PfRemoveGlobalFilterFromInterface
    0x7FULL, // PfSetLogBuffer
    0x7FULL, // PfTestPacket
    0x7FULL, // PfUnBindInterface
    0x7FULL, // ResolveIpNetEntry2
    0x7FULL, // ResolveNeighbor
    0x7FULL, // RestoreMediaSense
    0x7FULL, // SendARP
    0x7FULL, // SetAdapterIpAddress
    0x7FULL, // SetCurrentThreadCompartmentId
    0x7FULL, // SetCurrentThreadCompartmentScope
    0x7FULL, // SetDnsSettings
    0x7FULL, // SetIfEntry
    0x7FULL, // SetInterfaceDnsSettings
    0x7FULL, // SetIpForwardEntry
    0x7FULL, // SetIpForwardEntry2
    0x7FULL, // SetIpInterfaceEntry
    0x7FULL, // SetIpNetEntry
    0x7FULL, // SetIpNetEntry2
    0x7FULL, // SetIpStatistics
    0x7FULL, // SetIpStatisticsEx
    0x7FULL, // SetIpTTL
    0x7FULL, // SetJobCompartmentId
    0x7FULL, // SetNetworkInformation
    0x7FULL, // SetPerTcp6ConnectionEStats
    0x7FULL, // SetPerTcp6ConnectionStats
    0x7FULL, // SetPerTcpConnectionEStats
    0x7FULL, // SetPerTcpConnectionStats
    0x7FULL, // SetSessionCompartmentId
    0x7FULL, // SetTcpEntry
    0x7FULL, // SetUnicastIpAddressEntry
    0x7FULL, // UnenableRouter
    0x7FULL, // do_echo_rep
    0x7FULL, // do_echo_req
    0x0ULL, // if_indextoname
    0x0ULL, // if_nametoindex
    0x7FULL, // register_icmp
};
