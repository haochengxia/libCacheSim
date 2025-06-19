#include <pybind11/pybind11.h>

#include <iostream>

#include "cache_init.hpp"
#include "utils.hpp"
#include "libCacheSim.h"

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

struct CacheDeleter {
  void operator()(cache_t* ptr) const {
    if (ptr) ptr->cache_free(ptr);
  }
};

struct RequestDeleter {
  void operator()(request_t* ptr) const {
    if (ptr) free_request(ptr);
  }
};

struct ReaderDeleter {
  void operator()(reader_t* ptr) const {
    if (ptr) close_trace(ptr);
  }
};

namespace py = pybind11;

PYBIND11_MODULE(_libcachesim, m) {
  m.doc() = R"pbdoc(
        libCacheSim Python bindings
        --------------------------

        .. currentmodule:: libcachesim

        .. autosummary::
           :toctree: _generate

           TODO(haocheng): add meaningful methods
    )pbdoc";

  py::enum_<trace_type_e>(m, "TraceType")
      .value("CSV_TRACE", trace_type_e::CSV_TRACE)
      .value("PLAIN_TXT_TRACE", trace_type_e::PLAIN_TXT_TRACE)
      .value("BIN_TRACE", trace_type_e::BIN_TRACE)
      .value("VSCSI_TRACE", trace_type_e::VSCSI_TRACE)
      .export_values();

  // *************** structs ***************
  /**
   * @brief Cache structure
   */
  py::class_<cache_t, std::unique_ptr<cache_t, CacheDeleter>>(m, "Cache")
      .def_readwrite("n_req", &cache_t::n_req)
      .def_readwrite("n_obj", &cache_t::n_obj)
      .def_readwrite("occupied_byte", &cache_t::occupied_byte)
      .def_readwrite("cache_size", &cache_t::cache_size)
      // methods
      .def("get", [](cache_t& self, const request_t& req) {
        return self.get(&self, &req);
      });

  /**
   * @brief Request structure
   */
  py::class_<request_t, std::unique_ptr<request_t, RequestDeleter>>(m, "Request")
      .def(py::init([]() {
        return new_request();
      }))
      .def_readwrite("clock_time", &request_t::clock_time)
      .def_readwrite("hv", &request_t::hv)
      .def_readwrite("obj_id", &request_t::obj_id)
      .def_readwrite("obj_size", &request_t::obj_size)
      .def_readwrite("op", &request_t::op);

  /**
   * @brief Reader structure
   */
  py::class_<reader_t>(m, "Reader")
      .def_readwrite("n_read_req", &reader_t::n_read_req)
      .def_readwrite("n_total_req", &reader_t::n_total_req)
      .def_readwrite("trace_path", &reader_t::trace_path)
      .def_readwrite("file_size", &reader_t::file_size)
      // methods
      .def("get_wss", [](reader_t& self, bool ignore_obj_size) {

        int64_t wss_obj = 0, wss_byte = 0;
        cal_working_set_size(&self, &wss_obj, &wss_byte);
        return ignore_obj_size ? wss_obj : wss_byte;
      }, py::arg("ignore_obj_size") = false, 
      R"pbdoc(
            Get the working set size of the trace.

            Args:
                ignore_obj_size (bool): Whether to ignore the object size.

            Returns:
                int: The working set size of the trace.
      )pbdoc")
      .def("__iter__", [](reader_t& self) -> reader_t& { return self; })
      .def("__next__", [](reader_t& self) {
        auto req = std::unique_ptr<request_t, RequestDeleter>(new_request());
        int ret = read_one_req(&self, req.get());
        if (ret != 0) {
          throw py::stop_iteration();
        }
        // std::cout << "Read request: " << req->obj_id
        //           << ", size: " << req->obj_size << std::endl;
        return req;
      });

  py::class_<reader_init_param_t>(m, "reader_init_param_t")
      .def(py::init<>())
      .def_readwrite("time_field", &reader_init_param_t::time_field)
      .def_readwrite("obj_id_field", &reader_init_param_t::obj_id_field)
      .def_readwrite("obj_size_field", &reader_init_param_t::obj_size_field)
      .def_readwrite("delimiter", &reader_init_param_t::delimiter)
      .def_readwrite("has_header", &reader_init_param_t::has_header);

  // *************** functions ***************
  /**
   * @brief Open a trace file for reading
   */
  m.def(
      "open_trace",
      [](const std::string& trace_path, int type, const py::object& params) {
        reader_init_param_t init_param = {};
        if (py::isinstance<py::dict>(params)) {
          py::dict dict_params = params.cast<py::dict>();
          init_param.time_field = dict_params["time_field"].cast<int>();
          init_param.obj_id_field = dict_params["obj_id_field"].cast<int>();
          init_param.obj_size_field = dict_params["obj_size_field"].cast<int>();
          init_param.delimiter =
              dict_params["delimiter"].cast<std::string>()[0];
          init_param.has_header = dict_params["has_header"].cast<bool>();
        } else if (!params.is_none()) {
          init_param.time_field = py::getattr(params, "time_field").cast<int>();
          init_param.obj_id_field =
              py::getattr(params, "obj_id_field").cast<int>();
          init_param.obj_size_field =
              py::getattr(params, "obj_size_field").cast<int>();
          init_param.delimiter =
              py::getattr(params, "delimiter").cast<std::string>()[0];
          init_param.has_header =
              py::getattr(params, "has_header").cast<bool>();
        }
        reader_t* ptr = open_trace(
            trace_path.c_str(), static_cast<trace_type_e>(type), &init_param);
        return std::unique_ptr<reader_t, ReaderDeleter>(ptr);
      },
      py::arg("trace_path"), py::arg("type"),
      py::arg("reader_init_param") = py::none(),
      R"pbdoc(
            Open a trace file for reading.

            Args:
                trace_path (str): Path to the trace file.
                type (int): Type of the trace (e.g., CSV_TRACE).
                reader_init_param (Union[dict, reader_init_param_t, None]): Initialization parameters for the reader.

            Returns:
                Reader: A new reader instance for the trace.
        )pbdoc");

  /**
   * @brief Generic function to create a cache instance.
   */
  m.def(
      "create_cache",
      [](const std::string& eviction_algo,
         const uint64_t cache_size, const std::string& eviction_params,
         bool consider_obj_metadata) {
        cache_t* ptr = create_sim_cache(
            eviction_algo.c_str(), cache_size,
            eviction_params.c_str(), consider_obj_metadata);
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("eviction_algo"), py::arg("cache_size"),
      py::arg("eviction_params"), py::arg("consider_obj_metadata"),
      R"pbdoc(
            Create a cache instance.

            Args:
                eviction_algo (str): Eviction algorithm to use (e.g., "LRU", "FIFO", "Random").
                cache_size (int): Size of the cache in bytes.
                eviction_params (str): Additional parameters for the eviction algorithm.
                consider_obj_metadata (bool): Whether to consider object metadata in eviction decisions.

            Returns:
                Cache: A new cache instance.
        )pbdoc");

  /* TODO(haocheng): should we support all parameters in the common_cache_params_t? (hash_power, etc.) */
  
  // Currently supported eviction algorithms with direct initialization:
  //   - "ARC"
  //   - "Clock"
  //   - "FIFO"
  //   - "LRB"
  //   - "LRU"
  //   - "S3FIFO"
  //   - "Sieve"
  //   - "ThreeLCache"
  //   - "TinyLFU"
  //   - "TwoQ"

  /**
   * @brief Create a ARC cache instance.
   */
  m.def(
      "ARC_init",
      [](uint64_t cache_size) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = ARC_init(cc_params, nullptr);
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"),
      R"pbdoc(
            Create a ARC cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
      )pbdoc");

  /**
   * @brief Create a Clock cache instance.
   */
  m.def(
      "Clock_init",
      [](uint64_t cache_size, long int n_bit_counter, long int init_freq) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        // assemble the cache specific parameters
        std::string cache_specific_params =
            "n-bit-counter=" + std::to_string(n_bit_counter) + "," +
            "init-freq=" + std::to_string(init_freq);

        cache_t* ptr = Clock_init(cc_params, cache_specific_params.c_str());
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"), py::arg("n_bit_counter") = 1,
      py::arg("init_freq") = 0,
      R"pbdoc(
            Create a Clock cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
                n_bit_counter (int): Number of bits for counter (default: 1).
                init_freq (int): Initial frequency value (default: 0).

            Returns:
                Cache: A new Clock cache instance.
      )pbdoc");

  /**
   * @brief Create a FIFO cache instance.
   */
  m.def(
      "FIFO_init",
      [](uint64_t cache_size) {
        // Construct common cache parameters
        common_cache_params_t cc_params = {.cache_size = cache_size};
        // FIFO no specific parameters, so we pass nullptr
        cache_t* ptr = FIFO_init(cc_params, nullptr);
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"),
      R"pbdoc(
            Create a FIFO cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.

            Returns:
                Cache: A new FIFO cache instance.
      )pbdoc");

#ifdef ENABLE_LRB
  /**
   * @brief Create a LRB cache instance.
   */
  m.def(
      "LRB_init",
      [](uint64_t cache_size, std::string objective) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = LRB_init(cc_params, ("objective=" + objective).c_str());
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"), py::arg("objective") = "byte-miss-ratio",
      R"pbdoc(
            Create a LRB cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
                objective (str): Objective function to optimize (default: "byte-miss-ratio").

            Returns:
                Cache: A new LRB cache instance.
      )pbdoc");
#else
  // TODO(haocheng): add a dummy function to avoid the error when LRB is not enabled
  m.def(
      "LRB_init",
      [](uint64_t cache_size, std::string objective) {
        throw std::runtime_error("LRB is not enabled");
      },
      py::arg("cache_size"), py::arg("objective") = "byte-miss-ratio");
#endif

  /**
   * @brief Create a LRU cache instance.
   */
  m.def(
      "LRU_init",
      [](uint64_t cache_size) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = LRU_init(cc_params, nullptr);
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"),
      R"pbdoc(
            Create a LRU cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.

            Returns:
                Cache: A new LRU cache instance.
      )pbdoc");

  /**
   * @brief Create a S3FIFO cache instance.
   */
  m.def(
      "S3FIFO_init",
      [](uint64_t cache_size, double fifo_size_ratio, double ghost_size_ratio,
         int move_to_main_threshold) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = S3FIFO_init(
            cc_params,
            ("fifo-size-ratio=" + std::to_string(fifo_size_ratio) + "," +
             "ghost-size-ratio=" + std::to_string(ghost_size_ratio) + "," +
             "move-to-main-threshold=" + std::to_string(move_to_main_threshold))
                .c_str());
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"), py::arg("fifo_size_ratio") = 0.10,
      py::arg("ghost_size_ratio") = 0.90, py::arg("move_to_main_threshold") = 2,
      R"pbdoc(
            Create a S3FIFO cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
                fifo_size_ratio (float): Ratio of FIFO size to cache size (default: 0.10).
                ghost_size_ratio (float): Ratio of ghost size to cache size (default: 0.90).
                move_to_main_threshold (int): Threshold for moving to main queue (default: 2).

            Returns:
                Cache: A new S3FIFO cache instance.
      )pbdoc");

  /**
   * @brief Create a Sieve cache instance.
   */
  m.def(
      "Sieve_init",
      [](uint64_t cache_size) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = Sieve_init(cc_params, nullptr);
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"),
      R"pbdoc(
            Create a Sieve cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.

            Returns:
                Cache: A new Sieve cache instance.
      )pbdoc");

#ifdef ENABLE_3L_CACHE
  /**
   * @brief Create a ThreeL cache instance.
   */
  m.def(
      "ThreeLCache_init",
      [](uint64_t cache_size, std::string objective) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr =
            ThreeLCache_init(cc_params, ("objective=" + objective).c_str());
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"), py::arg("objective") = "byte-miss-ratio",
      R"pbdoc(
            Create a ThreeL cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
                objective (str): Objective function to optimize (default: "byte-miss-ratio"). 

            Returns:
                Cache: A new ThreeL cache instance.
      )pbdoc");
#else
  // TODO(haocheng): add a dummy function to avoid the error when ThreeLCache is not enabled
  m.def(
      "ThreeLCache_init",
      [](uint64_t cache_size, std::string objective) {
        throw std::runtime_error("ThreeLCache is not enabled");
      },
      py::arg("cache_size"), py::arg("objective") = "byte-miss-ratio");
#endif

  /**
   * @brief Create a TinyLFU cache instance.
   */
  // mark evivtion parsing need change
  m.def(
      "TinyLFU_init",
      [](uint64_t cache_size, std::string main_cache, double window_size) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = WTinyLFU_init(
            cc_params, ("main-cache=" + main_cache + "," +
                        "window-size=" + std::to_string(window_size))
                           .c_str());
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"), py::arg("main_cache") = "SLRU",
      py::arg("window_size") = 0.01,
      R"pbdoc(
            Create a TinyLFU cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
                main_cache (str): Main cache to use (default: "SLRU").
                window_size (float): Window size for TinyLFU (default: 0.01).

            Returns:
                Cache: A new TinyLFU cache instance.
      )pbdoc");

  /**
   * @brief Create a TwoQ cache instance.
   */
  m.def(
      "TwoQ_init",
      [](uint64_t cache_size, double Ain_size_ratio, double Aout_size_ratio) {
        common_cache_params_t cc_params = {.cache_size = cache_size};
        cache_t* ptr = TwoQ_init(
            cc_params,
            ("Ain-size-ratio=" + std::to_string(Ain_size_ratio) + "," +
             "Aout-size-ratio=" + std::to_string(Aout_size_ratio))
                .c_str());
        return std::unique_ptr<cache_t, CacheDeleter>(ptr);
      },
      py::arg("cache_size"), py::arg("Ain_size_ratio") = 0.25,
      py::arg("Aout_size_ratio") = 0.5,
      R"pbdoc(
            Create a TwoQ cache instance.

            Args:
                cache_size (int): Size of the cache in bytes.
                Ain_size_ratio (float): Ratio of A-in size to cache size (default: 0.25).
                Aout_size_ratio (float): Ratio of A-out size to cache size (default: 0.5).

            Returns:
                Cache: A new TwoQ cache instance.
      )pbdoc");

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}