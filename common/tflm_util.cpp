/* TFLM model inspection helpers */

#include <cstdio>
#include <cstdint>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/schema/schema_utils.h"

#include "tflm_util.h"

namespace {

void PrintTensor(unsigned index, const tflite::Tensor* tensor) {
  if (tensor == nullptr) {
    printf("    [%u] <invalid tensor>\n", index);
    return;
  }

  const char* name = tensor->name() ? tensor->name()->c_str() : "<unnamed>";
  printf("    [%u] %s\n", index, name);
  printf("        type=%s, dims=[", tflite::EnumNameTensorType(tensor->type()));

  size_t elements = 1;
  const auto* shape = tensor->shape();
  if (shape != nullptr) {
    for (unsigned d = 0; d < shape->size(); d++) {
      const int32_t dim = shape->Get(d);
      printf("%s%ld", d ? ", " : "", (long)dim);
      elements *= (dim > 0) ? (size_t)dim : 0;
    }
  }

  printf("], bytes=%u\n",
         (unsigned)(elements * tflite::TensorTypeGetSize(tensor->type())));
}

void PrintTensorList(const char* label,
                     const flatbuffers::Vector<int32_t>* indices,
                     const flatbuffers::Vector<flatbuffers::Offset<tflite::Tensor>>* tensors) {
  const unsigned count = indices ? indices->size() : 0;
  printf("  [TFLM] %s (%u):\n", label, count);
  for (unsigned i = 0; i < count; i++) {
    const int32_t tensor_index = indices->Get(i);
    const bool valid = tensors != nullptr && tensor_index >= 0 &&
                       (unsigned)tensor_index < tensors->size();
    PrintTensor(i, valid ? tensors->Get(tensor_index) : nullptr);
  }
}

}  // namespace

void TflmUtil::print_model_specs(const tflite::Model* model) {
  if (model == nullptr) {
    printf("  [TFLM] ERROR: print_model_specs: model is null\n");
    return;
  }

  const auto* subgraphs = model->subgraphs();
  if (subgraphs == nullptr || subgraphs->size() == 0) {
    printf("  [TFLM] ERROR: print_model_specs: model has no subgraphs\n");
    return;
  }

  printf("  [TFLM] ---- Model specs (subgraphs=%u) ----\n",
         (unsigned)subgraphs->size());

  /* Inputs and outputs of the main graph */
  const tflite::SubGraph* main_graph = subgraphs->Get(0);
  PrintTensorList("Inputs", main_graph->inputs(), main_graph->tensors());
  PrintTensorList("Outputs", main_graph->outputs(), main_graph->tensors());

  /* Operators: one entry per operator code, counted over all subgraphs */
  const auto* opcodes = model->operator_codes();
  const unsigned opcode_count = opcodes ? opcodes->size() : 0;

  unsigned total_nodes = 0;
  for (unsigned s = 0; s < subgraphs->size(); s++) {
    const auto* ops = subgraphs->Get(s)->operators();
    total_nodes += ops ? ops->size() : 0;
  }

  printf("  [TFLM] Operators (%u unique, %u nodes):\n", opcode_count, total_nodes);
  for (unsigned c = 0; c < opcode_count; c++) {
    unsigned uses = 0;
    for (unsigned s = 0; s < subgraphs->size(); s++) {
      const auto* ops = subgraphs->Get(s)->operators();
      if (ops == nullptr) {
        continue;
      }
      for (unsigned o = 0; o < ops->size(); o++) {
        if (ops->Get(o)->opcode_index() == c) {
          uses++;
        }
      }
    }

    const tflite::OperatorCode* opcode = opcodes->Get(c);
    const tflite::BuiltinOperator code = tflite::GetBuiltinCode(opcode);
    if (code == tflite::BuiltinOperator_CUSTOM) {
      const char* custom = opcode->custom_code() ? opcode->custom_code()->c_str() : "?";
      printf("    [%u] CUSTOM(%s) x%u\n", c, custom, uses);
    } else {
      printf("    [%u] %s (v%ld) x%u\n", c, tflite::EnumNameBuiltinOperator(code),
             (long)opcode->version(), uses);
    }
  }

  printf("  [TFLM] ---------------------------------\n");
}

void TflmUtil::print_arena_usage(const tflite::MicroInterpreter* interpreter) {
  if (interpreter == nullptr) {
    printf("  [TFLM] ERROR: print_arena_usage: interpreter is null\n");
    return;
  }

  printf("  [TFLM] Arena used: %u bytes\n",
         (unsigned)interpreter->arena_used_bytes());
}
