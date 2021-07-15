import FWCore.ParameterSet.Config as cms

sonic_deepmet = cms.EDProducer("DeepMETSonicProducer",
    Client = cms.PSet(
        timeout = cms.untracked.uint32(300),
        modelName = cms.string("deepmet"),
        mode = cms.string("Async"),
        modelConfigPath = cms.FileInPath("HeterogeneousCore/SonicTriton/data/models/deepmet/config.pbtxt"),
        modelVersion = cms.string("1"),
        verbose = cms.untracked.bool(False),
        allowedTries = cms.untracked.uint32(0),
    ),
    pf_src = cms.InputTag("packedPFCandidates"),
)
