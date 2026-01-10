struct Light {
	vec4 position_radius; // x, y ,z, radius
	vec4 color_strength; // r g b intensity
	vec4 direction_type; // x y z type
	vec4 params; // inner cone, outer cone, shadow map idx, enabled 
};
