#!/bin/bash

LOCALTOP=$1

tmpFile=$(mktemp -p ${LOCALTOP} RetryActionDiffLogXXXXXXXX.log)
cmsRun ${LOCALTOP}/src/HeterogeneousCore/SonicTriton/test/tritonTest_cfg.py \
  --modules TritonGraphProducer --models gat_test \
  --maxEvents 2 --unittest --device cpu --retryAction diff --verbose \
  > "$tmpFile" 2>&1
status=$?

if ! grep -q "Retry type: RetryActionDiffServer" "$tmpFile"; then
  echo "Expected retry type log line not found" >&2
  cat "$tmpFile"
  rm -f "$tmpFile"
  exit 1
fi

rm -f "$tmpFile"
exit $status


