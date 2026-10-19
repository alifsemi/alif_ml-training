/* TFLM model inspection helpers */
#ifndef TFLM_UTIL_H
#define TFLM_UTIL_H

namespace tflite {
struct Model;
class MicroInterpreter;
}

class TflmUtil {
 public:
  /**
   * @brief Print the model's input and output tensors (name, datatype,
   *        dimensions, size in bytes) and the operators used by the graph
   *        with their node counts. Custom operators (for example the
   *        Vela-generated "ethos-u" op) are printed with their custom code.
   *        Reads only the flatbuffer, so it can be called before or
   *        after AllocateTensors().
   * @param model  Model returned by tflite::GetModel()
   */
  static void print_model_specs(const tflite::Model* model);

  /**
   * @brief Print how much of the tensor arena the interpreter uses.
   *        Only meaningful after a successful AllocateTensors().
   * @param interpreter  Interpreter with allocated tensors
   */
  static void print_arena_usage(const tflite::MicroInterpreter* interpreter);
};

#endif /* TFLM_UTIL_H */
