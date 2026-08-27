import os
from enum import Enum
import json
import numpy as np
import pickle
import torch
import triton_python_backend_utils as pb_utils

class InputDumpSetting(Enum):
    NEVER = 0
    ALWAYS = 1
    ON_FAILURE = 2

class TritonPythonModel:
    def initialize(self, args):
        # Location to dump replay data as configured
        self.replay_dump_dir = "/dumps"
        self.device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")

        model_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model.pt")
        self.model = torch.jit.load(model_path, map_location=self.device).to(self.device)
        self.model.eval()

        model_config = json.loads(args["model_config"])
        self.model_name = model_config["name"]
        
        self.input_names = [i["name"] for i in model_config["input"]]
        self.output_names = [i["name"] for i in model_config["output"]]

        # Triton's Torch backend requires a "__<number>" suffix on tensor names to indicate
        # model's expected ordering of input and output. Use this to infer ordering.
        self.input_names.sort(key=lambda name: int(name.split("__")[-1]))
        self.output_names.sort(key=lambda name: int(name.split("__")[-1]))

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

        with torch.no_grad():
            for request in requests:
                err_msg = "none"
                request_inputs = {name: pb_utils.get_input_tensor_by_name(request, name).as_numpy() for name in self.input_names}
                
                try:
                    model_inputs = [torch.from_numpy(request_inputs[name]).to(self.device) for name in self.input_names]
                    output_tensors = self.model(*model_inputs)

                    # Treat as single tensor if only one ouptut, or as tuple otherwise
                    if (len(self.output_names) == 1):
                        output_tensor = output_tensors.cpu().numpy().astype(self.output_dtypes[0])
                        outputs = [pb_utils.Tensor(self.output_names[0], output_tensor)] # outputs must be list
                    else:
                        outputs = []
                        for idx, name in enumerate(self.output_names):
                            output_tensor = output_tensors[idx].cpu().numpy().astype(self.output_dtypes[idx])
                            outputs.append(pb_utils.Tensor(name, output_tensor))
                    
                    response = pb_utils.InferenceResponse(output_tensors=outputs)

                except Exception as ex:
                    err_msg = f"Error during inference: {str(ex)}"
                    response = pb_utils.InferenceResponse(error=pb_utils.TritonError(err_msg))

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
