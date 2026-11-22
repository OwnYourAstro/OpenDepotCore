# Core tools

This directory holds the code generators, codeplug schema tools, and the CLI
harness:

- `extract_pe_strings.py` — dump Windows RT_STRING resources from a PE image.
- `bind_table_fields.py` — join CPS table columns to runtime field IDs/offsets.
- `gen_field_catalog.py` — generate the C++ `FieldCatalog` from the schemas.
- `decode_field_schema.py` — decode named fields from a codeplug model.
- `parse_table_defs.py` — parse Rui/CgIsh `.tbl` column definitions.
- `gen_model_db.py` — regenerate `data/model_db.bin` from the model catalog.
- `gen_flashcode_db.py` — regenerate `data/platforms.bin`.
- `opendepot-cli` — thin CLI over the public API, used by CI and humans.

During M0 these still live in the legacy `OpenDepot` repository under
`tools/` and `src/main_cli.cpp`.
