#ifndef CAMERA_H
#define CAMERA_H

/* -------------------------------------------------------------------------- */
/* Camera                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
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

static void camera_update_axes(Camera *cam) {
    float yaw_rad = cam->yaw   * (float)M_PI / 180.0f;
    float pitch_rad = cam->pitch * (float)M_PI / 180.0f;

    Vec3 front = vec3(cosf(yaw_rad) * cosf(pitch_rad),
                      sinf(pitch_rad),
                      sinf(yaw_rad) * cosf(pitch_rad));
    cam->front = vec3_normalize(front);
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up    = vec3_normalize(vec3_cross(cam->right, cam->front));
}

static void camera_init(Camera *cam) {
    cam->position         = vec3(0.0f, 0.0f, 3.0f);
    cam->world_up         = vec3(0.0f, 1.0f, 0.0f);
    cam->yaw              = -90.0f;
    cam->pitch            = 0.0f;
    cam->movement_speed   = 6.0f;
    cam->mouse_sensitivity = 0.1f;
    camera_update_axes(cam);
}

static void camera_process_mouse(Camera *cam, float x_offset, float y_offset) {
    x_offset *= cam->mouse_sensitivity;
    y_offset *= cam->mouse_sensitivity;

    cam->yaw   += x_offset;
    cam->pitch += y_offset;

    if (cam->pitch > 89.0f)  cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;

    camera_update_axes(cam);
}

static Mat4 camera_view_matrix(Camera *cam) {
    Vec3 target = vec3_add(cam->position, cam->front);
    return mat4_look_at(cam->position, target, cam->up);
}

#endif /* CAMERA_H */