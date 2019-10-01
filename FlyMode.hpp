#pragma once

#include "Mode.hpp"
#include "FlyLevel.hpp"
#include "DrawLines.hpp"

#include <memory>

struct FlyMode : Mode {
	FlyMode(FlyLevel const &level);
	virtual ~FlyMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//The (starting shape of the) level:
	FlyLevel const &start;

	//The (active, being-played) level:
	void restart();
	FlyLevel level;
	bool won = false;

	int goalsHit = 0;
	float speed_target = 0.0f;
	float speed_full = 50.0f;
	float speed_normal = 1.0f;
	float speed_slow = 1.0f;

	float time_run = 0.0f;
	float time_record = 0.0f;

	//Current control signals:
	struct {
		bool forward = false;
		bool backward = false;
		bool left = false;
		bool right = false;
	} controls;

	//fly around for collsion debug:
	bool DEBUG_fly = false;
	bool DEBUG_show_geometry = false;
	bool DEBUG_show_collision = false;

	//some debug drawing done during update:
	std::unique_ptr< DrawLines > DEBUG_draw_lines;
};
