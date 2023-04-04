//
// Created by ari on 4/3/23.
//
#include <c2d2.h>

#include "c2d/c2d_transformer.hpp"

c2d_driver::c2d_driver(uint32_t max_num_surface_templates, uint32_t max_num_object_lists) {
    C2D_DRIVER_SETUP_INFO setup = {0};
    setup.max_surface_template_needed = max_num_surface_templates;
    setup.max_object_list_needed      = max_num_object_lists;
    c2dDriverInit(&setup);
}

c2d_driver::~c2d_driver() {
    c2dDriverDeInit();
}
