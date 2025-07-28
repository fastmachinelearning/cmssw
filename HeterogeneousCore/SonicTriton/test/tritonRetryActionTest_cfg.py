import FWCore.ParameterSet.Config as cms
import os, sys, json
from argparse import ArgumentParser, ArgumentDefaultsHelpFormatter
from Configuration.ProcessModifiers.enableSonicTriton_cff import enableSonicTriton

process = cms.Process('tritonTest', enableSonicTriton)

process.load("HeterogeneousCore.SonicTriton.TritonService_cff")

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(10))

process.source = cms.Source("EmptySource")

process.myProducer = cms.EDProducer("TritonGraphProducer",
    # minimal inputs for testing
    nodeMin = cms.uint32(1),
    nodeMax = cms.uint32(10),
    edgeMin = cms.uint32(20),
    edgeMax = cms.uint32(40),
    # client setup
    Client = cms.PSet(
        # This address is fake to force an error
        address = cms.string("localhost:9999"),
        mode = cms.string("Sync"),
        # This is your retry logic
        Retry = cms.VPSet(
            cms.PSet(
                retryType = cms.string("RetryActionDiffServer"),
                # The address of the real server will be filled in by the TritonService
                diff_server_url = cms.string(""),
                diff_server_token = cms.string("")
            )
        )
    )
)

process.p = cms.Path(process.myProducer)

process.load('FWCore/MessageService/MessageLogger_cfi')
process.MessageLogger.cerr.FwkReport.reportEvery = 1
# enable verbose output for everything
process.MessageLogger.cerr.default = cms.untracked.PSet(
    limit = cms.untracked.int32(10000000)
)