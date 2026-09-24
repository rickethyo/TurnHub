"""Check generated HTTP responses and shared fixtures without extra dependencies.

This deliberately checks only the JSON Schema keywords used by these contracts;
an unknown validation keyword fails rather than silently going unchecked.
"""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
PROTOCOL = ROOT / "protocol"
TYPES = {"object": dict, "array": list, "string": str, "integer": int,
         "boolean": bool, "null": type(None)}
KEYWORDS = {"$schema", "$id", "title", "description", "type", "const", "enum",
            "required", "properties", "additionalProperties", "items", "minimum",
            "maximum", "minLength", "maxLength", "minItems", "maxItems", "pattern"}


def check(value, schema):
    assert not set(schema) - KEYWORDS, set(schema) - KEYWORDS
    if "type" in schema:
        kinds = schema["type"]
        if isinstance(kinds, str):
            kinds = [kinds]
        assert any(type(value) is TYPES[kind] for kind in kinds), (value, kinds)
    if "const" in schema:
        assert value == schema["const"]
    if "enum" in schema:
        assert value in schema["enum"]
    if isinstance(value, dict):
        assert set(schema.get("required", [])) <= value.keys()
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            assert value.keys() <= properties.keys(), value.keys() - properties.keys()
        for key, item in value.items():
            if key in properties:
                check(item, properties[key])
    elif isinstance(value, list):
        assert schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", len(value))
        for item in value:
            check(item, schema["items"])
    elif isinstance(value, str):
        assert schema.get("minLength", 0) <= len(value) <= schema.get("maxLength", len(value))
        if "pattern" in schema:
            assert re.search(schema["pattern"], value)
    elif type(value) is int:
        assert schema.get("minimum", value) <= value <= schema.get("maximum", value)


def validate(path):
    value = json.loads(path.read_text())
    schema_name = ("info-v1" if "deviceType" in value else
                   "accessibility-v1" if "ledStyle" in value else
                   "control-result-v1" if "ok" in value else "state-v0.1")
    check(value, json.loads((PROTOCOL / (schema_name + ".schema.json")).read_text()))
    if schema_name == "state-v0.1":
        numbers = [player["playerNumber"] for player in value["players"]]
        assert len(numbers) == len(set(numbers))
        for field in ["activePlayer", "starterPlayer", "winnerPlayer"]:
            assert value[field] is None or value[field] in numbers
        for player in value["players"]:
            assert all(d["sourcePlayer"] in numbers for d in player["commanderDamage"])
    elif schema_name == "accessibility-v1":
        limits = value["limits"]
        assert value["winHoldMs"] >= value["longPressMs"] + limits["minGapMs"]
        assert value["longPressMs"] % limits["stepMs"] == 0 == value["winHoldMs"] % limits["stepMs"]
    elif schema_name == "control-result-v1":
        assert value["ok"] == (value["status"] == "ACCEPTED")
        assert ("message" if value["ok"] else "error") in value


generated = sorted((Path(__file__).parent / "build").glob("client-*.json"))
assert len(generated) >= 8, "Run native scenarios first"
examples = sorted((PROTOCOL / "examples").glob("*.response.json"))
for path in generated + examples:
    validate(path)
print(f"PASS: {len(generated)} actual HTTP/projection responses and {len(examples)} shared fixtures")
