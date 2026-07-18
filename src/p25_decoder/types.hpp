#pragma once

// P25 Phase 1 Common Air Interface constants (TIA-102.BAAA)

// Data Unit IDs (DUID), carried in the NID
#define P25_DUID_HDU    0x0   // Header Data Unit
#define P25_DUID_TDU    0x3   // Simple Terminator Data Unit (no Link Control)
#define P25_DUID_LDU1   0x5   // Logical Link Data Unit 1 (voice + Link Control)
#define P25_DUID_TSDU   0x7   // Trunking Signalling Data Unit
#define P25_DUID_LDU2   0xA   // Logical Link Data Unit 2 (voice + Encryption Sync)
#define P25_DUID_PDU    0xC   // Packet Data Unit
#define P25_DUID_TDULC  0xF   // Terminator Data Unit with Link Control

// Link Control Format (LCF) values we care about
#define P25_LCF_GROUP        0x00   // Group Voice Channel User
#define P25_LCF_UNIT_TO_UNIT 0x03   // Unit to Unit Voice Channel User

// Encryption algorithm IDs (ALGID)
#define P25_ALGID_UNENCRYPTED 0x80  // clear / no encryption
