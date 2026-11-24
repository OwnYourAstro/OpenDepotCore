# Model manifests

Generated analysis artifacts for clean Depot-generated codeplugs.

## Generate

```powershell
OpenDepotCore\build\local\Release\corpus-analyzer.exe `
  C:\Users\Rose\Desktop\depot_codeplugs `
  OpenDepotCore\build\analysis\depot_portable.json

python OpenDepotCore\tools\analyze_corpus.py `
  OpenDepotCore\build\analysis\depot_portable.json `
  --model H46UCF9PW6BN `
  --option P25_MDC_Conv_Data `
  --manifest OpenDepotCore\schema\manifests\H46UCF9PW6BN-P25_MDC_Conv_Data.json
```

## Manifest fields

- `baseTags`: record tags present in every option profile for the model.
- `records[tag].instances`: instance numbers observed in the clean profile.
- `records[tag].minLen`, `maxLen`: observed payload lengths.
- `records[tag].capacity`: instance count and maximum payload length from
  `ModelReadRequests` (the hardware-validated read set); absent when no read
  set exists for the model.
- `records[tag].sections`: **observed** section assignment from
  `CodeplugModel`. This is not authoritative until it is replaced by a Depot
  CPS tree/block manifest.
- `records[tag].fieldCount`: number of valid field-schema entries with an exact
  record tag.
- `records[tag].block`: verified block name, when known.

`corpus-analyzer` also verifies save/reload round-trips. The portable corpus is
currently 392/392 byte-identical; the mobile corpus is 72/72.
