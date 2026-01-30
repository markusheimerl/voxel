#include "camera.h"
#include "player.h"
#include <math.h>

#define DEG_TO_RAD(deg) ((deg) * (float)M_PI / 180.0f)
#define PITCH_LIMIT 89.0f

static void camera_update_axes(Camera *cam) {
    float yaw_rad = DEG_TO_RAD(cam->yaw);
    float pitch_rad = DEG_TO_RAD(cam->pitch);
    float cos_pitch = cosf(pitch_rad);

    Vec3 front = vec3(
        cosf(yaw_rad) * cos_pitch,
        sinf(pitch_rad),
        sinf(yaw_rad) * cos_pitch
    );
    
    cam->front = vec3_normalize(front);
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up = vec3_normalize(vec3_cross(cam->right, cam->front));
}

void camera_init(Camera *cam) {
    cam->position = vec3(0.0f, 0.0f, 3.0f);
    cam->world_up = vec3(0.0f, 1.0f, 0.0f);
    cam->yaw = -90.0f;
    cam->pitch = 0.0f;
    cam->movement_speed = 6.0f;
    cam->mouse_sensitivity = 0.1f;
    camera_update_axes(cam);
}

void camera_process_mouse(Camera *cam, float x_offset, float y_offset) {
    cam->yaw += x_offset * cam->mouse_sensitivity;
    cam->pitch += y_offset * cam->mouse_sensitivity;

    if (cam->pitch > PITCH_LIMIT) cam->pitch = PITCH_LIMIT;
    if (cam->pitch < -PITCH_LIMIT) cam->pitch = -PITCH_LIMIT;

    camera_update_axes(cam);
}

Mat4 camera_view_matrix(Camera *cam) {
    Vec3 target = vec3_add(cam->position, cam->front);
    return mat4_look_at(cam->position, target, cam->up);
}

void camera_follow_player(Camera *cam, const Player *player) {
    if (!cam || !player) return;
    cam->position = vec3_add(player->position, vec3(0.0f, player_eye_height(), 0.0f));
}