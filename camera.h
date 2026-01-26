#ifndef CAMERA_H
#define CAMERA_H

#include "math.h"

/* -------------------------------------------------------------------------- */
/* Camera                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct Camera {
    Vec3 position;
    Vec3 front;
    Vec3 up;
    Vec3 right;
    Vec3 world_up;
    float yaw;
    float pitch;
    float movement_speed;
    float mouse_sensitivity;
} Camera;

void camera_init(Camera *cam);
void camera_process_mouse(Camera *cam, float x_offset, float y_offset);
Mat4 camera_view_matrix(Camera *cam);

#endif /* CAMERA_H */