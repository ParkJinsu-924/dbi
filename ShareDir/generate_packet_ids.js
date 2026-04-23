const fs = require('fs');
const path = require('path');

const SCRIPT_DIR = __dirname;
const PROTO_DIR = path.join(SCRIPT_DIR, 'proto');

const CPP_ENUM_OUT = path.join(SCRIPT_DIR, '..', 'mmosvr', 'ServerCore', 'Packet', 'PacketUtils.h');
const CPP_TRAITS_OUT = path.join(SCRIPT_DIR, '..', 'mmosvr', 'ServerCore', 'Packet', 'PacketIdTraits.h');
const CS_PARTIAL_OUT = path.join(SCRIPT_DIR, '..', 'AlbionClient', 'UOP1_Project', 'Assets', 'Scripts', 'MMO', 'Proto', 'PacketIds.cs');
const PY_OUT = path.join(SCRIPT_DIR, '..', 'debug_tool', 'packet_ids.py');

// Proto files to scan, in order. Messages get sequential IDs.
// server.proto messages are server-only (excluded from C# PacketIds).
const PROTO_FILES = [
    { file: 'login.proto', serverOnly: false },
    { file: 'game.proto', serverOnly: false },
    { file: 'server.proto', serverOnly: true },
];

// Parse a proto file and extract message names (top-level only)
function parseMessages(filename) {
    const content = fs.readFileSync(path.join(PROTO_DIR, filename), 'utf8');
    const messages = [];
    // Match top-level message declarations (not nested)
    const lines = content.split('\n');
    let braceDepth = 0;
    for (const line of lines) {
        const trimmed = line.trim();
        if (braceDepth === 0) {
            const match = trimmed.match(/^message\s+(\w+)\s*\{?/);
            if (match) {
                messages.push(match[1]);
            }
        }
        for (const ch of trimmed) {
            if (ch === '{') braceDepth++;
            if (ch === '}') braceDepth--;
        }
    }
    return messages;
}

// Convert PascalCase message name to UPPER_SNAKE_CASE for enum
// e.g. C_Login -> C_LOGIN, S_PlayerMove -> S_PLAYER_MOVE, SS_ValidateToken -> SS_VALIDATE_TOKEN
function toEnumName(msgName) {
    // Split on existing underscores first
    const parts = msgName.split('_');
    return parts.map(part => {
        // Insert underscore before uppercase letters (for PascalCase parts)
        return part.replace(/([a-z])([A-Z])/g, '$1_$2').toUpperCase();
    }).join('_');
}

// --- Collect all messages with IDs ---
let nextId = 1;
const allPackets = [];

for (const { file, serverOnly } of PROTO_FILES) {
    const messages = parseMessages(file);
    for (const msg of messages) {
        // Skip non-packet types (Vector3, Timestamp, etc.)
        if (!msg.startsWith('C_') && !msg.startsWith('S_') && !msg.startsWith('SS_')) continue;
        allPackets.push({
            proto: msg,
            enumName: toEnumName(msg),
            id: nextId++,
            serverOnly,
        });
    }
}

// --- Generate C++ PacketUtils.h ---
function generateCppEnum() {
    let lines = [];
    lines.push('#pragma once');
    lines.push('');
    lines.push('#include "Utils/Types.h"');
    lines.push('');
    lines.push('// Auto-generated from ShareDir/generate_packet_ids.js');
    lines.push('enum class PacketId : uint32');
    lines.push('{');
    for (const pkt of allPackets) {
        lines.push(`\t${pkt.enumName} = ${pkt.id},`);
    }
    lines.push('};');
    lines.push('');
    return lines.join('\r\n');
}

// --- Generate C++ PacketIdTraits.h ---
function generateCppTraits() {
    let lines = [];
    lines.push('#pragma once');
    lines.push('');
    lines.push('#include "Packet/PacketUtils.h"');
    lines.push('');
    lines.push('// Auto-generated from ShareDir/generate_packet_ids.js');
    lines.push('');
    lines.push('namespace Proto');
    lines.push('{');
    for (const pkt of allPackets) {
        lines.push(`\tclass ${pkt.proto};`);
    }
    lines.push('}');
    lines.push('');
    lines.push('template<typename T>');
    lines.push('struct PacketIdTraits;');
    lines.push('');
    lines.push('#define PACKET_ID_TRAIT(MsgType, PktId) \\');
    lines.push('template<> struct PacketIdTraits<Proto::MsgType> \\');
    lines.push('{ static constexpr PacketId Id = PacketId::PktId; };');
    lines.push('');
    for (const pkt of allPackets) {
        lines.push(`PACKET_ID_TRAIT(${pkt.proto}, ${pkt.enumName})`);
    }
    lines.push('');
    lines.push('#undef PACKET_ID_TRAIT');
    lines.push('');
    return lines.join('\r\n');
}

// --- Generate C# PacketIds.cs (partial classes) ---
function generateCsPartial() {
    let lines = [];
    lines.push('// Auto-generated from ShareDir/generate_packet_ids.js');
    lines.push('namespace Proto');
    lines.push('{');
    for (const pkt of allPackets.filter(p => !p.serverOnly)) {
        lines.push(`    public partial class ${pkt.proto} { public const uint PacketId = ${pkt.id}; }`);
    }
    lines.push('}');
    lines.push('');
    return lines.join('\r\n');
}

// --- Generate Python packet_ids.py ---
function generatePyIds() {
    let lines = [];
    lines.push('# Auto-generated from ShareDir/generate_packet_ids.js');
    lines.push('');
    for (const pkt of allPackets.filter(p => !p.serverOnly)) {
        lines.push(`${pkt.enumName} = ${pkt.id}`);
    }
    lines.push('');
    lines.push('# Reverse mapping: id -> message class name (for dispatch)');
    lines.push('PACKET_NAMES = {');
    for (const pkt of allPackets.filter(p => !p.serverOnly)) {
        lines.push(`    ${pkt.id}: "${pkt.proto}",`);
    }
    lines.push('}');
    lines.push('');
    return lines.join('\r\n');
}

// --- Write files ---
fs.writeFileSync(CPP_ENUM_OUT, generateCppEnum());
console.log('[OK] ' + CPP_ENUM_OUT);

fs.writeFileSync(CPP_TRAITS_OUT, generateCppTraits());
console.log('[OK] ' + CPP_TRAITS_OUT);

fs.writeFileSync(CS_PARTIAL_OUT, generateCsPartial());
console.log('[OK] ' + CS_PARTIAL_OUT);

fs.writeFileSync(PY_OUT, generatePyIds());
console.log('[OK] ' + PY_OUT);

console.log('[OK] PacketId generation complete. Total: ' + allPackets.length + ' packets');
