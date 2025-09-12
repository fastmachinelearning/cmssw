import FWCore.ParameterSet.Config as cms
import os, sys, json
from HeterogeneousCore.SonicTriton.customize import getDefaultClientPSet, getParser, getOptions, applyOptions, applyClientOptions

# module/model correspondence
models = {
    "TritonImageProducer": ["inception_graphdef", "densenet_onnx"],
    "TritonGraphProducer": ["gat_test"],
    "TritonGraphFilter": ["gat_test"],
    "TritonGraphAnalyzer": ["gat_test"],
    "TritonIdentityProducer": ["ragged_io"],
}

# other choices
allowed_modes = ["Async","PseudoAsync","Sync"]

parser = getParser()
parser.add_argument("--modules", metavar=("MODULES"), default=["TritonGraphProducer"], nargs='+', type=str, choices=list(models), help="list of modules to run (choices: %(choices)s)")
parser.add_argument("--models", default=["gat_test"], nargs='+', type=str, help="list of models (same length as modules, or just 1 entry if all modules use same model)")
parser.add_argument("--mode", default="Async", type=str, choices=allowed_modes, help="mode for client")
parser.add_argument("--brief", default=False, action="store_true", help="briefer output for graph modules")
parser.add_argument("--unittest", default=False, action="store_true", help="unit test mode: reduce input sizes")
parser.add_argument("--testother", default=False, action="store_true", help="also test gRPC communication if shared memory enabled, or vice versa")
parser.add_argument("--noShm", default=False, action="store_true", help="disable shared memory")
parser.add_argument("--compression", default="", type=str, choices=allowed_compression, help="enable I/O compression")
parser.add_argument("--ssl", default=False, action="store_true", help="enable SSL authentication for server communication")
parser.add_argument("--device", default="auto", type=str.lower, choices=allowed_devices, help="specify device for fallback server")
parser.add_argument("--container", default="apptainer", type=str.lower, choices=allowed_containers, help="specify container for fallback server")
parser.add_argument("--tries", default=0, type=int, help="number of retries for failed request")
parser.add_argument("--retryAction", default="same", type=str, choices=["same","diff"], help="retry policy: same server or different server")
options = parser.parse_args()

options = getOptions(parser, verbose=True)

# check models and modules
if len(options.modules)!=len(options.models):
    # assigning to VarParsing.multiplicity.list actually appends to existing value(s)
    if len(options.models)==1: options.models = [options.models[0]]*(len(options.modules))
    else: raise ValueError("Arguments for modules and models must have same length")
for im,module in enumerate(options.modules):
    model = options.models[im]
    if model not in models[module]:
        raise ValueError("Unsupported model {} for module {}".format(model,module))

from Configuration.ProcessModifiers.enableSonicTriton_cff import enableSonicTriton
process = cms.Process('tritonTest',enableSonicTriton)

process.load("HeterogeneousCore.SonicTriton.TritonService_cff")
process.source = cms.Source("EmptySource")

# Let it run
process.p = cms.Path()

modules = {
    "Producer": cms.EDProducer,
    "Filter": cms.EDFilter,
    "Analyzer": cms.EDAnalyzer,
}

defaultClient = applyClientOptions(getDefaultClientPSet().clone(), options)
keepMsgs = []
if options.verbose or options.verboseDiscovery:
    keepMsgs.append('TritonDiscovery')
if options.verbose or options.verboseClient:
    keepMsgs.append('TritonClient')
if options.verbose or options.verboseService:
    keepMsgs.append('TritonService')
if options.verbose:
    # ensure RetryActionDiffServer messages are not suppressed if emitted
    keepMsgs.append('RetryActionDiffServer')

for im,module in enumerate(options.modules):
    model = options.models[im]
    Module = [obj for name,obj in modules.items() if name in module][0]
    setattr(process, module,
        Module(module,
            Client = defaultClient.clone(
                mode = cms.string(options.mode),
                preferredServer = cms.untracked.string(""),
                modelName = cms.string(model),
                modelVersion = cms.string(""),
                modelConfigPath = cms.FileInPath("HeterogeneousCore/SonicTriton/data/models/{}/config.pbtxt".format(model)),
                verbose = cms.untracked.bool(options.verbose or options.verboseClient),
                useSharedMemory = cms.untracked.bool(not options.noShm),
                compression = cms.untracked.string(options.compression),
                Retry = (
                  cms.VPSet(
                    cms.PSet(
                      retryType = cms.string('RetrySameServerAction'),
                      allowedTries = cms.untracked.uint32(options.tries)
                    )
                  ) if options.retryAction == 'same' else cms.VPSet(
                    cms.PSet(
                      retryType = cms.string('RetryActionDiffServer')
                    )
                  )
                )
            )
        )
    )
    processModule = getattr(process, module)
    if module=="TritonImageProducer":
        processModule.batchSize = cms.int32(1)
        processModule.topN = cms.uint32(5)
        processModule.imageList = cms.FileInPath("HeterogeneousCore/SonicTriton/data/models/{}/{}_labels.txt".format(model,model.split('_')[0]))
    elif "TritonGraph" in module:
        if options.unittest:
            # reduce input size for unit test
            processModule.nodeMin = cms.uint32(1)
            processModule.nodeMax = cms.uint32(10)
            processModule.edgeMin = cms.uint32(20)
            processModule.edgeMax = cms.uint32(40)
        else:
            processModule.nodeMin = cms.uint32(100)
            processModule.nodeMax = cms.uint32(4000)
            processModule.edgeMin = cms.uint32(8000)
            processModule.edgeMax = cms.uint32(15000)
        processModule.brief = cms.bool(options.brief)
    process.p += processModule
    if options.verbose:
        print("Retry type:", ('RetrySameServerAction' if options.retryAction == 'same' else 'RetryActionDiffServer'))
    if options.verbose or options.verboseClient:
        keepMsgs.extend([module,module+':TritonClient'])
    if options.testother:
        # clone modules to test both gRPC and shared memory
        _module2 = module+"GRPC" if processModule.Client.useSharedMemory else "SHM"
        setattr(process, _module2,
            processModule.clone(
                Client = dict(useSharedMemory = not processModule.Client.useSharedMemory)
            )
        )
        processModule2 = getattr(process, _module2)
        process.p += processModule2

process = applyOptions(process, options)
