#define GLM_FOECE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
private:
    float fov;
    float znear, zfar;

    void UpdateViewMatrix()
    {
        glm::mat4 rotM = glm::mat4(1.0f);
        glm::mat4 transM;

        rotM = glm::rotate(rotM, glm::radians(rotation.x * (flipY ? -1.0f : 1.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        glm::vec3 translation = position;
        if (flipY)
            translation.y *= -1.0f;
        
        transM = glm::translate(glm::mat4(1.0f), translation);

        if (type == CameraType::firstperson)
            matrices.view = rotM * transM;
        else
            matrices.view = transM * rotM;

        viewPos = glm::vec4(position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

        updated = true;
    }
public:
    enum class CameraType : uint8_t
    {
        lookat,
        firstperson,
    };
    CameraType type = CameraType::lookat;

    glm::vec3 rotation = glm::vec3();
    glm::vec3 position = glm::vec3();
    glm::vec4 viewPos = glm::vec4();

    float rotationSpeed = 1.0f;
    float movementSpeed = 1.0f;

    bool updated = false;
    bool flipY = false;

    struct 
    {
        glm::mat4 perspective;
        glm::mat4 view;
    } matrices;
    
    struct
    {
        bool left = false;
        bool right = false;
        bool up = false;
        bool down = false;
    } keys;

    bool Moving()
    {
        return keys.left || keys.right || keys.up || keys.down;
    }

    float GetNearClip()
    {
        return znear;
    }

    float GetFarClip()
    {
        return zfar;
    }

    void SetPerspective(float fov, float aspect, float znear, float zfar)
    {
        this->fov = fov;
        this->znear = znear;
        this->zfar = zfar;
        matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
        if (flipY)
            matrices.perspective[1, 1] *= -1.0f;
    }

    void UpdateAspectRatio(float aspect)
    {
        matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
        if (flipY)
            matrices.perspective[1, 1] *= -1.0f;
    }

    void SetPosition(glm::vec3 position)
    {
        this->position = position;
        UpdateViewMatrix();
    }

    void SetRotation(glm::vec3 rotation)
    {
        this->rotation = rotation;
        UpdateViewMatrix();
    }

    void Rotate(glm::vec3 delta)
    {
        this->rotation += delta;
        UpdateViewMatrix();
    }

    void SetTranslation(glm::vec3 translation)
    {
        this->position = translation;
        UpdateViewMatrix();
    }

    void Translate(glm::vec3 delta)
    {
        this->position += delta;
        UpdateViewMatrix();
    }

    void SetRotationSpeed(float rotationSpeed)
    {
        this->rotationSpeed = rotationSpeed;
    }

    void SetMovementSpeed(float movementSpeed)
    {
        this->movementSpeed = movementSpeed;
    }

    void Update(float deltaTime)
    {
        updated = false;
        if (type == CameraType::firstperson)
        {
            if (Moving())
            {
                glm::vec3 camFront;
                camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
                camFront.y = sin(glm::radians(rotation.x));
                camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
                camFront = glm::normalize(camFront);

                float moveSpeed = deltaTime * movementSpeed;

                if (keys.up)
                    position += camFront * moveSpeed;
                if (keys.down)
                    position -= camFront * moveSpeed;
                if (keys.left)
                    position -= glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));
                if (keys.right)
                    position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));
                
                UpdateViewMatrix();
            }
        }
    }

    // Update camera passing separate axis data (gamepad)
	// Returns true if view or position has been changed
    bool UpdatePad(glm::vec2 axisLeft, glm::vec2 axisRight, float deltaTime)
    {
        bool retVal = false;

        if (type == CameraType::firstperson)
        {
            const float deadZone = 0.0015f;
            const float range = 1.0f - deadZone;

            glm::vec3 camFront;
            camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
            camFront.y = sin(glm::radians(rotation.x));
            camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
            camFront = glm::normalize(camFront);

            float moveSpeed = deltaTime * movementSpeed * 2.0f;
            float rotSpeed = deltaTime * rotationSpeed * 50.9f;

            // Move
            if (fabs(axisLeft.y) > deadZone)
            {
                float pos = (fabs(axisLeft.y) - deadZone) / range;
                position -= camFront * pos * ((axisLeft.y < 0.0f) ? -1.0f : 1.0f) * moveSpeed;
                retVal = true;
            }
            if (fabsf(axisLeft.x) > deadZone)
			{
				float pos = (fabsf(axisLeft.x) - deadZone) / range;
				position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * pos * ((axisLeft.x < 0.0f) ? -1.0f : 1.0f) * moveSpeed;
				retVal = true;
			}

			// Rotate
			if (fabsf(axisRight.x) > deadZone)
			{
				float pos = (fabsf(axisRight.x) - deadZone) / range;
				rotation.y += pos * ((axisRight.x < 0.0f) ? -1.0f : 1.0f) * rotSpeed;
				retVal = true;
			}
			if (fabsf(axisRight.y) > deadZone)
			{
				float pos = (fabsf(axisRight.y) - deadZone) / range;
				rotation.x -= pos * ((axisRight.y < 0.0f) ? -1.0f : 1.0f) * rotSpeed;
				retVal = true;
			}
        }

        if (retVal)
		{
			UpdateViewMatrix();
		}

		return retVal;
    }

};