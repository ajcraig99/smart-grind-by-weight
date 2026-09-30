#!/usr/bin/env python3
"""Validate params_default.json against params.schema.json and against the generator output.

Uses the `jsonschema` package when it is installed, otherwise a small built-in validator that
supports the subset of JSON Schema used by params.schema.json (type, required, properties,
additionalProperties, anyOf, const).
"""
import json
import sys


def validate(value, schema, path="$"):
    errors = []
    if "anyOf" in schema:
        results = [validate(value, s, path) for s in schema["anyOf"]]
        if not any(len(r) == 0 for r in results):
            errors.append("%s: matches none of anyOf (%s)" % (path, "; ".join(e for r in results for e in r)))
        return errors
    t = schema.get("type")
    if t == "object":
        if not isinstance(value, dict):
            return ["%s: expected object" % path]
        for key in schema.get("required", []):
            if key not in value:
                errors.append("%s: missing %s" % (path, key))
        props = schema.get("properties", {})
        for key, sub in value.items():
            if key in props:
                errors += validate(sub, props[key], "%s.%s" % (path, key))
            elif schema.get("additionalProperties", True) is False:
                errors.append("%s: unexpected property %s" % (path, key))
    elif t == "number":
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            errors.append("%s: expected number" % path)
    elif t == "string":
        if not isinstance(value, str):
            errors.append("%s: expected string" % path)
    if "const" in schema and value != schema["const"]:
        errors.append("%s: expected %r" % (path, schema["const"]))
    return errors


def main():
    defaults_path, schema_path = sys.argv[1], sys.argv[2]
    with open(defaults_path) as f:
        data = json.load(f)
    with open(schema_path) as f:
        schema = json.load(f)
    try:
        import jsonschema  # type: ignore

        jsonschema.validate(data, schema)
        print("validated with jsonschema")
    except ImportError:
        errors = validate(data, schema)
        if errors:
            print("\n".join(errors))
            return 1
        print("validated with the built-in subset validator")
    for name, entry in data.items():
        if not (entry["min"] <= entry["value"] <= entry["max"]):
            print("%s: default outside sweep range" % name)
            return 1
        if entry["source"] != "placeholder" and ":" not in entry["source"]:
            print("%s: source must be path:line or placeholder" % name)
            return 1
        s = schema["properties"][name]
        if s["x-default"] != entry["value"] or s["x-sweep-min"] != entry["min"] or s["x-sweep-max"] != entry["max"]:
            print("%s: schema and defaults disagree" % name)
            return 1
    if set(data) != set(schema["properties"]):
        print("parameter sets differ")
        return 1
    print("%d parameters ok" % len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
