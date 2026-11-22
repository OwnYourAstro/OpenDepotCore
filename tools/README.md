# Core tools

This directory will hold the code generators and the CLI harness after M1:

- `gen_model_db.py` — regenerate `data/model_db.bin` from the model catalog.
- `gen_flashcode_db.py` — regenerate `data/platforms.bin`.
- `opendepot-cli` — thin CLI over the public API, used by CI and humans.

During M0 these still live in the legacy `OpenDepot` repository under
`tools/` and `src/main_cli.cpp`.
