#pragma once

// Runtime CS2 schema dumper. Walks schemasystem.dll's CSchemaSystem to resolve
// class field offsets BY NAME live, so struct offsets self-heal across game
// updates instead of being hardcoded. Populates the global Offsets map.
//
// Only resolves SCHEMA class fields (C_* entities, services). Global RVAs
// (dwEntityList...), function sigs (TraceShape) and non-schema internals
// (bone indices, CUserCmd command angle) still need pattern scans / manual.
class SchemaDump {
public:
    // Resolve + register schema offsets. Returns number of fields set. Safe:
    // validates every read; a wrong schema layout writes nothing (no crash).
    static int Run();
};
