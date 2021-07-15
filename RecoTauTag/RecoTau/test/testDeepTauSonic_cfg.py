"""
cleaner version to run the DeepTau Sonic Producer only
for debugging performance
"""

## working points for the discriminator

workingPoints_ = {
    "e": {
        "VVVLoose": 0.0630386,
        "VVLoose": 0.1686942,
        "VLoose": 0.3628130,
        "Loose": 0.6815435,
        "Medium": 0.8847544,
        "Tight": 0.9675541,
        "VTight": 0.9859251,
        "VVTight": 0.9928449,
    },
    "mu": {
        "VLoose": 0.1058354,
        "Loose": 0.2158633,
        "Medium": 0.5551894,
        "Tight": 0.8754835,
    },
    "jet": {
        "VVVLoose": 0.2599605,
        "VVLoose": 0.4249705,
        "VLoose": 0.5983682,
        "Loose": 0.7848675,
        "Medium": 0.8834768,
        "Tight": 0.9308689,
        "VTight": 0.9573137,
        "VVTight": 0.9733927,
    },
}

def processDeepProducer(process, producer_name):
    postfix = ""
    import six
    for target,points in six.iteritems(workingPoints_):
        cut_expressions = []
        for index, (point,cut) in enumerate(six.iteritems(points)):
            cut_expressions.append(str(cut))

        setattr(getattr(process, producer_name+postfix), 'VS{}WP'.format(target), cms.vstring(*cut_expressions))


import FWCore.ParameterSet.Config as cms
from Configuration.ProcessModifiers.enableSonicTriton_cff import enableSonicTriton
from RecoTauTag.RecoTau.deeptauIdSonicProducer_cff import sonic_deeptau

process = cms.Process('DeepTau', enableSonicTriton)

# import of standard configurations
process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.EventContent.EventContent_cff')
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

process.maxEvents = cms.untracked.PSet(
    input=cms.untracked.int32(100)
)

# Input source
process.source = cms.Source("PoolSource",
                            fileNames=cms.untracked.vstring(
                                '/store/mc/RunIISummer20UL18MiniAOD/DYJetsToLL_M-50_TuneCP5_13TeV-madgraphMLM-pythia8/MINIAODSIM/106X_upgrade2018_realistic_v11_L1v1-v1/260000/03794341-C401-CC45-B5FC-D11264E449CE.root'
                            ),
                            )

process.load("HeterogeneousCore.SonicTriton.TritonService_cff")
process.TritonService.verbose = True
process.TritonService.servers.append(
    cms.PSet(
        name = cms.untracked.string("default"),
        address = cms.untracked.string("0.0.0.0"),
        port = cms.untracked.uint32(8021),
    )
)


process.deepTauProducer = sonic_deeptau.clone(
)
# add working points to the process
processDeepProducer(process, 'deepTauProducer')

process.p = cms.Path()
process.p += process.deepTauProducer

process.output = cms.OutputModule("PoolOutputModule",
                                  outputCommands=cms.untracked.vstring(
                                      'keep *'),
                                  fileName=cms.untracked.string(
                                      "DeepTauSonicTest.root")
                                  )
process.outpath  = cms.EndPath(process.output)

process.options = cms.untracked.PSet(
    wantSummary = cms.untracked.bool( True ),
    numberOfThreads = cms.untracked.uint32( 1 ),
    numberOfStreams = cms.untracked.uint32( 0 ),
    sizeOfStackForThreadsInKB = cms.untracked.uint32( 10*1024 )
)
