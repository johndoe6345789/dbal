#!/usr/bin/env python3
"""
Type generator for DBAL
Reads YAML entity schemas and generates TypeScript and C++ type definitions
"""

import argparse
import json
from pathlib import Path
from typing import Dict, Any, List
import sys

YAML_TO_TS_TYPE_MAP = {
    'uuid': 'string',
    'cuid': 'string',
    'string': 'string',
    'text': 'string',
    'email': 'string',
    'integer': 'number',
    'boolean': 'boolean',
    'datetime': 'Date',
    'bigint': 'bigint',
    'json': 'Record<string, unknown>',
    'enum': 'string',
}

YAML_TO_CPP_TYPE_MAP = {
    'uuid': 'std::string',
    'cuid': 'std::string',
    'string': 'std::string',
    'text': 'std::string',
    'email': 'std::string',
    'integer': 'int',
    'boolean': 'bool',
    'datetime': 'Timestamp',
    'bigint': 'Timestamp',
    'json': 'Json',
    'enum': 'std::string',
}

# Types that represent relations, not data fields — skip in struct generation
SKIP_TYPES = {'relationship'}


def load_entity_schemas(schema_dir: Path) -> Dict[str, Any]:
    """Load all entity YAML files (including nested, multi-doc schemas)."""
    entities = {}
    for json_file in (schema_dir / 'entities').rglob('*.json'):
        with open(json_file) as f:
            raw = json.load(f)
        # A file may contain a single entity object or an array of entities
        docs = raw if isinstance(raw, list) else [raw]
        for entity_data in docs:
            if not isinstance(entity_data, dict):
                continue
            entity_name = entity_data.get('entity')
            if not entity_name:
                continue
            entities[entity_name] = entity_data
    return entities


def generate_typescript_interface(entity_name: str, entity_data: Dict[str, Any]) -> str:
    """Generate TypeScript interface from entity schema"""
    lines = [f"export interface {entity_name} {{"]

    for field_name, field_data in entity_data['fields'].items():
        if field_data['type'] in SKIP_TYPES:
            continue
        field_type = YAML_TO_TS_TYPE_MAP.get(field_data['type'], 'unknown')
        optional = '?' if field_data.get('optional', False) else ''
        if field_data.get('nullable', False):
            field_type = f"{field_type} | null"

        if field_data['type'] == 'enum':
            enum_values = ' | '.join(f"'{v}'" for v in field_data['values'])
            field_type = enum_values

        lines.append(f"  {field_name}{optional}: {field_type}")

    lines.append("}")
    return '\n'.join(lines)


def generate_typescript_types(entities: Dict[str, Any], output_file: Path):
    """Generate TypeScript types file"""
    lines = ["// Generated types from JSON schemas - DO NOT EDIT MANUALLY\n"]
    
    for entity_name, entity_data in entities.items():
        lines.append(generate_typescript_interface(entity_name, entity_data))
        lines.append("")
    
    output_file.write_text('\n'.join(lines))
    print(f"✓ Generated TypeScript types: {output_file}")


# Field names that are legal in JSON and SQL but not as C++ member names.
# WorkflowNodeCondition.operator produced `std::string operator;`, which does
# not compile -- and because this header is built into the daemon, one such
# field broke the image build for every commit until it was noticed.
CPP_KEYWORDS = {
    'alignas', 'alignof', 'and', 'asm', 'auto', 'bitand', 'bitor', 'bool',
    'break', 'case', 'catch', 'char', 'class', 'compl', 'concept', 'const',
    'consteval', 'constexpr', 'constinit', 'continue', 'co_await',
    'co_return', 'co_yield', 'decltype', 'default', 'delete', 'do', 'double',
    'dynamic_cast', 'else', 'enum', 'explicit', 'export', 'extern', 'false',
    'float', 'for', 'friend', 'goto', 'if', 'inline', 'int', 'long', 'mutable',
    'namespace', 'new', 'noexcept', 'not', 'nullptr', 'operator', 'or',
    'private', 'protected', 'public', 'register', 'reinterpret_cast',
    'requires', 'return', 'short', 'signed', 'sizeof', 'static',
    'static_assert', 'static_cast', 'struct', 'switch', 'template', 'this',
    'thread_local', 'throw', 'true', 'try', 'typedef', 'typeid', 'typename',
    'union', 'unsigned', 'using', 'virtual', 'void', 'volatile', 'wchar_t',
    'while', 'xor',
}


def cpp_member_name(field_name: str) -> str:
    """A C++-safe member name. Trailing underscore is the conventional escape."""
    return f"{field_name}_" if field_name in CPP_KEYWORDS else field_name


def generate_cpp_struct(entity_name: str, entity_data: Dict[str, Any]) -> str:
    """Generate C++ struct from entity schema"""
    lines = [f"struct {entity_name} {{"]

    for field_name, field_data in entity_data['fields'].items():
        if field_data['type'] in SKIP_TYPES:
            continue
        field_type = YAML_TO_CPP_TYPE_MAP.get(field_data['type'], 'std::string')

        if field_data.get('optional', False) or field_data.get('nullable', False):
            field_type = f"std::optional<{field_type}>"

        member = cpp_member_name(field_name)
        if member != field_name:
            # The wire name stays as the schema wrote it; only the C++ member
            # is renamed, so anything mapping by JSON key is unaffected.
            lines.append(f"    // schema field: {field_name}")
        lines.append(f"    {field_type} {member};")

    lines.append("};")
    return '\n'.join(lines)


def generate_cpp_types(entities: Dict[str, Any], output_file: Path):
    """Generate C++ types header"""
    lines = [
        "// Generated types from JSON schemas - DO NOT EDIT MANUALLY",
        "#ifndef DBAL_GENERATED_TYPES_HPP",
        "#define DBAL_GENERATED_TYPES_HPP",
        "",
        "#include <string>",
        "#include <optional>",
        "#include <chrono>",
        "#include <map>",
        "#include <cstdint>",
        "",
        "namespace dbal {",
        "",
        "using Timestamp = std::chrono::system_clock::time_point;",
        "using Json = std::map<std::string, std::string>;",
        ""
    ]
    
    for entity_name, entity_data in entities.items():
        lines.append(generate_cpp_struct(entity_name, entity_data))
        lines.append("")
    
    lines.extend([
        "}  // namespace dbal",
        "",
        "#endif  // DBAL_GENERATED_TYPES_HPP"
    ])
    
    output_file.write_text('\n'.join(lines))
    print(f"✓ Generated C++ types: {output_file}")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='Generate TypeScript and C++ types from YAML entity schemas')
    parser.add_argument('--schema-dir', type=Path, help='Path to schema directory (contains entities/ subfolder)')
    parser.add_argument('--cpp-output', type=Path, help='Output path for C++ header')
    parser.add_argument('--ts-output', type=Path, help='Output path for TypeScript types')
    parser.add_argument('--cpp-only', action='store_true', help='Only generate C++ types')
    parser.add_argument('--ts-only', action='store_true', help='Only generate TypeScript types')
    args = parser.parse_args()

    dbal_root = Path(__file__).resolve().parents[3]
    schema_dir = args.schema_dir or (dbal_root / 'shared' / 'api' / 'schema')

    if not schema_dir.exists():
        print(f"Error: Schema directory not found: {schema_dir}", file=sys.stderr)
        sys.exit(1)

    print("Loading entity schemas...")
    entities = load_entity_schemas(schema_dir)
    print(f"Loaded {len(entities)} entities")

    if not args.ts_only:
        cpp_output = args.cpp_output or (dbal_root / 'production' / 'include' / 'dbal' / 'core' / 'types.generated.hpp')
        cpp_output.parent.mkdir(parents=True, exist_ok=True)
        generate_cpp_types(entities, cpp_output)

    if not args.cpp_only:
        ts_output = args.ts_output or (dbal_root / 'development' / 'src' / 'core' / 'foundation' / 'types' / 'types.generated.ts')
        ts_output.parent.mkdir(parents=True, exist_ok=True)
        generate_typescript_types(entities, ts_output)

    print("\n✓ Type generation complete!")


if __name__ == '__main__':
    main()
