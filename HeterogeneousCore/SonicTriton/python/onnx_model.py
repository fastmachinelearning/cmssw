import os
from enum import Enum
import json
import numpy as np
import pickle
import onnxruntime as rt
import triton_python_backend_utils as pb_utils

class InputDumpSetting(Enum):
    NEVER = 0
    ALWAYS = 1
    ON_FAILURE = 2

class TritonPythonModel:
    def initialize(self, args):
        # Location to dump replay data as configured
        self.replay_dump_dir = "/dumps"
        num_cpus = os.cpu_count() # should make this another custom config parameter?

        providers = ["CUDAExecutionProvider"]
        sess_options = rt.SessionOptions()
        sess_options.intra_op_num_threads = num_cpus
        sess_options.graph_optimization_level = rt.GraphOptimizationLevel.ORT_ENABLE_ALL

        model_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model.onnx")

        self.sess = rt.InferenceSession(model_path, sess_options=sess_options, providers=providers)

        self.input_names = [inp.name for inp in self.sess.get_inputs()]
        self.output_names = [out.name for out in self.sess.get_outputs()]

        model_config = json.loads(args["model_config"])
        self.model_name = model_config["name"]

        self.output_dtypes = []
        for out_name in self.output_names:
            out_config = pb_utils.get_output_config_by_name(model_config, out_name)
            self.output_dtypes.append(pb_utils.triton_string_to_numpy(out_config["data_type"]))

        # Configure input dumping based on custom model config parameter
        self.input_dump_setting = InputDumpSetting.NEVER
        config_dump_string = model_config.get("parameters", {}).get("dump_input", {}).get("string_value", "").lower()
        if (config_dump_string == "always"):
            self.input_dump_setting = InputDumpSetting.ALWAYS
        elif (config_dump_string == "on_failure"):
            self.input_dump_setting = InputDumpSetting.ON_FAILURE

    def execute(self, requests):
        responses = []

        for request in requests:
            err_msg = "none"
            request_inputs = {name: pb_utils.get_input_tensor_by_name(request, name).as_numpy() for name in self.input_names}

            try:
                pred_onnx = self.sess.run(self.output_names, request_inputs)

                if (len(self.output_names) == 1):
                    # outputs must be list
                    outputs = [pb_utils.Tensor(self.output_names[0], pred_onnx[0].astype(self.output_dtypes[0]))]
                else:
                    outputs = []
                    for idx, name in enumerate(self.output_names):
                        outputs.append(pb_utils.Tensor(name, pred_onnx[idx].astype(self.output_dtypes[idx])))

                response = pb_utils.InferenceResponse(output_tensors=outputs)
            
            except Exception as ex:
                err_msg = f"Error during inference: {str(ex)}"
                response = pb_utils.InferenceResponse(error = pb_utils.TritonError(err_msg))
            
            # Dump inputs if configured
            if (self.input_dump_setting == InputDumpSetting.ALWAYS) or (self.input_dump_setting == InputDumpSetting.ON_FAILURE and err_msg != "none"):
                req_id = request.request_id() # Returns a client-specified id or empty string
                if (req_id == ""):
                    req_id = "UNKNOWN_ID"
                
                err_dict = {
                    "id": req_id,
                    "model": self.model_name,
                    "message": err_msg,
                    "inputs": request_inputs
                }
                with open(f"{self.replay_dump_dir}/{req_id}.pkl", "wb") as f:
                    pickle.dump(err_dict, f)
            
            responses.append(response)
        
        return responses
