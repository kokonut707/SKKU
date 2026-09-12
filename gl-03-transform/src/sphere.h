#pragma once
#ifndef __SPHERE_H__
#define __SPHERE_H__

struct sphere_t
{
	vec3	center = vec3(0);	// 2D position for translation
	float	radius = 1.0f;		// radius
	float	theta = 0.0f;		// rotation angle
	float	distance = 0.0f;	// how far from sun?
	float	rotating_speed = 0.0f;		// 자전 속도
	float	orbit_speed = 0.0f;			// 공전 속도
	vec4	color;				// RGBA color in [0,1]
	mat4	model_matrix;		// modeling transformation

	// public functions
	void	update(float t);
};

inline std::vector<sphere_t> create_spheres()
{
	std::vector<sphere_t> spheres;
	sphere_t s;

	s = { s.center,130.0f,0.0f,0.0f,1.9969f,0.0f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Sun
	spheres.emplace_back(s);

	s = { s.center,4.0f,0.0f,150.0f,0.003f,47.8725f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Mercury
	spheres.emplace_back(s);

	s = { s.center,9.0f,0.0f,170.0f,0.0018f,35.0214f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Venus
	spheres.emplace_back(s);

	s = { s.center,10.0f,0.0f,200.0f,0.4651f,29.7859f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Earth
	spheres.emplace_back(s);

	s = { s.center,5.0f,0.0f,230.0f,0.2411f,24.1309f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Mars
	spheres.emplace_back(s);

	s = { s.center,75.f,0.0f,350.0f,12.6f,13.0697f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Jupiter
	spheres.emplace_back(s);

	s = { s.center,65.f,0.0f,520.0f,9.87f,9.6724f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Saturn
	spheres.emplace_back(s);

	s = { s.center,30.f,0.0f,650.0f,2.59f,6.8352f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Uranus
	spheres.emplace_back(s);

	s = { s.center,26.f,0.0f,720.0f,2.68f,5.4778f,vec4(1.0f,0.5f,0.5f,1.0f) }; // Neptune
	spheres.emplace_back(s);

	return spheres;
}

inline void sphere_t::update(float t)
{
	theta = t;
	float c = cos(theta * rotating_speed), s = sin(theta * rotating_speed);
	float x = center.x + distance * cos(theta * orbit_speed),
		  y = center.y + distance * sin(theta * orbit_speed),
		  z = center.z;

	// these transformations will be explained in later transformation lecture
	mat4 scale_matrix =
	{
		radius, 0, 0, 0,
		0, radius, 0, 0,
		0, 0, radius, 0,
		0, 0, 0, 1
	};

	mat4 rotation_matrix =
	{
		c,-s, 0, 0,
		s, c, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	mat4 translate_matrix =
	{
		1, 0, 0, x,
		0, 1, 0, y,
		0, 0, 1, z,
		0, 0, 0, 1
	};

	model_matrix = translate_matrix * rotation_matrix * scale_matrix;
}

#endif
