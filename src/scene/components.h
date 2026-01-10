#pragma once

#include "asset/model_manager.h"
#include "util/math.h"

#include <string>

struct Transform_Component {
	vec3 position = vec3(0.0f);
	vec3 rotation = vec3(0.0f);
	vec3 scale = vec3(1.0f);
	mat4 world_transform = mat4(1.0f);
	bool dirty = false;
	bool updated = false;
};

struct Name_Component {
	std::string string;

	const char* c_str() const { return string.c_str(); }
};

struct Physics_Component {
	
};

struct Model_Component {
	Model_Handle handle;
};

struct Motion {
	uint32_t a;
};

struct Light_Component {
	enum class Type { Point, Spot };

	Type type = Type::Point;
	vec3 color = vec3(1.0f);
	float intensity = 1.0f;
	float range = 10.0f;
	vec3 direction = vec3(0.0f, -1.0f, 0.0f);
	float inner_cone_angle = 30.0f;
	float outer_cone_angle = 45.0f;

	bool dirty = true;
};

struct Lifetime_Component {

};
