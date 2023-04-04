/**
 * <c2d_transformer.hpp>
 *
 * @brief   Utilities pertaining to transforming image buffers using the Qualcomm C2D api
 *
 *
 * @author  Ari Young
 * @date    3 April 2023
 */

#ifndef QRB5165_CAMERA_SERVER_C2D_TRANSFORMER_H
#define QRB5165_CAMERA_SERVER_C2D_TRANSFORMER_H

#include <c2d2.h>
#include <cstdint>

/**
 * The `c2d_driver` class is an RAII guard around the c2d initialization/de-initialization functions. When constructed,
 * it will initialize the c2d driver; when destructed, it will de-initialize the c2d driver.
 */
class c2d_driver {
public:
    // TODO: figure out good values for max_surface_template / max_object_list
    explicit c2d_driver(uint32_t max_num_surface_templates = 10, uint32_t max_num_object_lists = 10);
    ~c2d_driver();
};

/**
 * The `c2d_transformer` class represents the external interface for interacting with C2D. Before any transformations
 * take place, the C2D driver should be initialized by instantiating an instance of \ref c2d_driver
 */
class c2d_transformer {
public:
    c2d_transformer();

};


#endif //QRB5165_CAMERA_SERVER_C2D_TRANSFORMER_H
