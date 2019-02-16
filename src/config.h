#define VSYNC                           1
#define SCROLL_THRESHOLD                0.1
#define MAX_MESSAGES                    4
#define DAY_LENGTH                      600
#define INVERT_MOUSE                    0
#define CRAFT_KEY_FORWARD               GLFW_KEY_W
#define CRAFT_KEY_BACKWARD              GLFW_KEY_S
#define CRAFT_KEY_LEFT                  GLFW_KEY_A
#define CRAFT_KEY_RIGHT                 GLFW_KEY_D
#define CRAFT_KEY_ITEM_NEXT             GLFW_KEY_E
#define CRAFT_KEY_ITEM_PREV             GLFW_KEY_R
#define CRAFT_KEY_ZOOM                  GLFW_KEY_Z
#define CRAFT_KEY_ORTHO                 GLFW_KEY_F
#define CRAFT_KEY_COMMAND               GLFW_KEY_SLASH
#define CRAFT_KEY_JUMP                  GLFW_KEY_SPACE
#define CRAFT_KEY_CROUCH                GLFW_KEY_LEFT_SHIFT
#define CRAFT_KEY_FLY                   GLFW_KEY_TAB
#define RENDER_CHUNK_RADIUS             8
#define MAX_CHUNKS                      8192
#define MAX_TEXT_LENGTH                 256
#define ALIGN_LEFT                      0
#define ALIGN_CENTER                    1
#define ALIGN_RIGHT                     2
#define CHUNK_SIZE                      32
#define XZ_SIZE                         (CHUNK_SIZE * 2)
#define XZ_LO                           (CHUNK_SIZE)
#define XZ_HI                           (CHUNK_SIZE * 2)
#define Y_SIZE                          256
#define XYZ(x, y, z)                    ((y) * XZ_SIZE * XZ_SIZE + (x) * XZ_SIZE + (z))
#define XZ(x, z)                        ((x) * XZ_SIZE + (z))
#define PI                              3.14159265359
#define DEGREES(radians)                ((radians) * 180 / PI)
#define RADIANS(degrees)                ((degrees) * PI / 180)
#define ABS(x)                          ((x) < 0 ? (-(x)) : (x))
#define MIN(a, b)                       ((a) < (b) ? (a) : (b))
#define MAX(a, b)                       ((a) > (b) ? (a) : (b))
#define SIGN(x)                         (((x) > 0) - ((x) < 0))
