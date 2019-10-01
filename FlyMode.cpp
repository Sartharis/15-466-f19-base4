#include "FlyMode.hpp"
#include "DrawLines.hpp"
#include "LitColorTextureProgram.hpp"
#include "Mesh.hpp"
#include "Sprite.hpp"
#include "DrawSprites.hpp"
#include "data_path.hpp"
#include "Sound.hpp"
#include "collide.hpp"
#include "gl_errors.hpp"

//for glm::pow(quaternion, float):
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <iostream>

Load< SpriteAtlas > font_atlas(LoadTagDefault, []() -> SpriteAtlas const * {
	return new SpriteAtlas(data_path("trade-font"));
});

FlyMode::FlyMode( FlyLevel const &level_) : start(level_), level(level_) {
	restart();
}

FlyMode::~FlyMode() {
}

bool FlyMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if( evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_BACKSPACE ) {
		restart();
		return true;
	}
	if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_1) {
		DEBUG_show_geometry = !DEBUG_show_geometry;
		return true;
	}
	if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_2) {
		DEBUG_show_collision = !DEBUG_show_collision;
		return true;
	}



	if (evt.type == SDL_MOUSEMOTION) {
			//based on trackball camera control from ShowMeshesMode:
			//figure out the motion (as a fraction of a normalized [-a,a]x[-1,1] window):
			glm::vec2 delta;
			float speed_mod = ( glm::mix( 2.0f, 0.2f, glm::length( level.player.velocity ) / speed_full ) );
			delta.x = evt.motion.xrel / float(window_size.x) * 2.0f * speed_mod;
			delta.x *= float(window_size.y) / float(window_size.x);
			delta.y = evt.motion.yrel / float(window_size.y) * -2.0f * speed_mod;

			level.player.view_azimuth -= delta.x;
			level.player.view_elevation -= delta.y;

			level.player.view_azimuth /= 2.0f * 3.1415926f;
			level.player.view_azimuth -= std::round(level.player.view_azimuth);
			level.player.view_azimuth *= 2.0f * 3.1415926f;

			level.player.view_elevation = std::max(-85.0f / 180.0f * 3.1415926f, level.player.view_elevation);
			level.player.view_elevation = std::min( 85.0f / 180.0f * 3.1415926f, level.player.view_elevation);

			return true;
	}
	
	return false;
}

void FlyMode::update(float elapsed) {
	//NOTE: turn this on to fly the sphere instead of rolling it -- makes collision debugging easier
	{ //player motion:
		//build a shove from controls:

		const Uint32 state = SDL_GetMouseState( NULL, NULL );

		if( !won )
		{
			time_run += elapsed;
		}

		if( state & SDL_BUTTON(SDL_BUTTON_LEFT) )
		{
			speed_target = speed_full;
		}
		else if( state & SDL_BUTTON( SDL_BUTTON_RIGHT ) )
		{
			speed_target = speed_slow;
		}
		else
		{
			speed_target = speed_normal;
		}


		glm::vec3 shove = glm::vec3(0.0f);
		shove.y = 1.0f;

		if (shove != glm::vec3(0.0f)) {
			//if (DEBUG_fly) {
				//DEBUG: fly mode -- shove is fully camera-relative:
				glm::mat3 frame = glm::mat3_cast(
					glm::angleAxis( level.player.view_azimuth, glm::vec3(0.0f, 0.0f, 1.0f) )
					* glm::angleAxis(-level.player.view_elevation + 0.5f * 3.1415926f, glm::vec3(1.0f, 0.0f, 0.0f) ));
				shove = frame * glm::vec3(shove.x, 0.0f, -shove.y);
			/*} else {
				//make shove relative to camera direction (but ignore up/down tilt):
				shove = glm::normalize(shove);
				float ca = std::cos(level.player.view_azimuth);
				float sa = std::sin(level.player.view_azimuth);
				
				shove = glm::vec3(ca, sa, level.player.view_elevation ) * shove.x + glm::vec3(-sa, ca, level.player.view_elevation ) * shove.y;
			}*/
		}

		shove *= speed_target;

		//update player using shove:

		//get references to player position/rotation because those are convenient to have:
		glm::vec3 &position = level.player.transform->position;
		glm::vec3 &velocity = level.player.velocity;
		glm::quat &rotation = level.player.transform->rotation;
		glm::vec3 &rotational_velocity = level.player.rotational_velocity;

		rotational_velocity *= std::pow(0.5f, elapsed / 2.0f);

		velocity = glm::mix(shove, velocity, std::pow(0.5f, elapsed / 0.25f));

		//set up a new draw_lines object for DEBUG drawing:
		if (!DEBUG_draw_lines) {
			DEBUG_draw_lines.reset(new DrawLines(glm::mat4(1.0f)));
		}
		
		//collide against level:
		float remain = elapsed;
		for (int32_t iter = 0; iter < 10; ++iter) {
			if (remain == 0.0f) break;

			float sphere_radius = 1.0f; //player sphere is radius-1
			glm::vec3 sphere_sweep_from = position;
			glm::vec3 sphere_sweep_to = position + velocity * remain;

			glm::vec3 sphere_sweep_min = glm::min(sphere_sweep_from, sphere_sweep_to) - glm::vec3(sphere_radius);
			glm::vec3 sphere_sweep_max = glm::max(sphere_sweep_from, sphere_sweep_to) + glm::vec3(sphere_radius);

			bool collided = false;
			float collision_t = 1.0f;
			glm::vec3 collision_at = glm::vec3(0.0f);
			glm::vec3 collision_out = glm::vec3(0.0f);

			for( auto const &collider : level.goal_colliders ) {
				glm::mat4x3 collider_to_world = collider.transform->make_local_to_world();
				{ //Early discard:
				  // check if AABB of collider overlaps AABB of swept sphere:

				  //(1) compute bounding box of collider in world space:
					glm::vec3 local_center = 0.5f * ( collider.mesh->max + collider.mesh->min );
					glm::vec3 local_radius = 0.5f * ( collider.mesh->max - collider.mesh->min );

					glm::vec3 world_min = collider_to_world * glm::vec4( local_center, 1.0f )
						- glm::abs( local_radius.x * collider_to_world[0] )
						- glm::abs( local_radius.y * collider_to_world[1] )
						- glm::abs( local_radius.z * collider_to_world[2] );
					glm::vec3 world_max = collider_to_world * glm::vec4( local_center, 1.0f )
						+ glm::abs( local_radius.x * collider_to_world[0] )
						+ glm::abs( local_radius.y * collider_to_world[1] )
						+ glm::abs( local_radius.z * collider_to_world[2] );

					//(2) check if bounding boxes overlap:
					bool can_skip = !collide_AABB_vs_AABB( sphere_sweep_min, sphere_sweep_max, world_min, world_max );

					//draw to indicate result of check:
					if( iter == 0 && DEBUG_draw_lines && DEBUG_show_geometry ) {
						DEBUG_draw_lines->draw_box( glm::mat4x3(
							0.5f * ( world_max.x - world_min.x ), 0.0f, 0.0f,
							0.0f, 0.5f * ( world_max.y - world_min.y ), 0.0f,
							0.0f, 0.0f, 0.5f * ( world_max.z - world_min.z ),
							0.5f * ( world_max.x + world_min.x ), 0.5f * ( world_max.y + world_min.y ), 0.5f * ( world_max.z + world_min.z )
						), ( can_skip ? glm::u8vec4( 0x88, 0x88, 0x88, 0xff ) : glm::u8vec4( 0x88, 0x88, 0x00, 0xff ) ) );
					}

					if( can_skip ) {
						//AABBs do not overlap; skip detailed check:
						continue;
					}
				}

				//Full (all triangles) test:
				assert( collider.mesh->type == GL_TRIANGLES ); //only have code for TRIANGLES not other primitive types
				for( GLuint v = 0; v + 2 < collider.mesh->count; v += 3 ) {
					//get vertex positions from associated positions buffer:
					//  (and transform to world space)
					glm::vec3 a = collider_to_world * glm::vec4( collider.buffer->positions[collider.mesh->start + v + 0], 1.0f );
					glm::vec3 b = collider_to_world * glm::vec4( collider.buffer->positions[collider.mesh->start + v + 1], 1.0f );
					glm::vec3 c = collider_to_world * glm::vec4( collider.buffer->positions[collider.mesh->start + v + 2], 1.0f );
					//check triangle:
					bool did_collide = collide_swept_sphere_vs_triangle(
						sphere_sweep_from, sphere_sweep_to, sphere_radius,
						a, b, c,
						&collision_t, &collision_at, &collision_out );

					

					if( did_collide && !collider.isCollided) {
						const_cast<FlyLevel::GoalCollider&>(collider).isCollided = true;
						goalsHit++;
						if( goalsHit == level.goal_colliders.size() )
						{
							won = true;
							time_record = time_record > 0.0f ? glm::min( time_record, time_run ) : time_run;
						}
					}

					//draw to indicate result of check:
					if( iter == 0 && DEBUG_draw_lines ) {
						glm::u8vec4 color = ( did_collide ? glm::u8vec4( 0x88, 0x00, 0x00, 0xff ) : glm::u8vec4( 0x88, 0x88, 0x00, 0xff ) );
						if( DEBUG_show_geometry || ( did_collide && DEBUG_show_collision ) ) {
							DEBUG_draw_lines->draw( a, b, color );
							DEBUG_draw_lines->draw( b, c, color );
							DEBUG_draw_lines->draw( c, a, color );
						}
						//do a bit more to highlight colliding triangles (otherwise edges can be over-drawn by non-colliding triangles):
						if( did_collide && DEBUG_show_collision ) {
							glm::vec3 m = ( a + b + c ) / 3.0f;
							DEBUG_draw_lines->draw( glm::mix( a, m, 0.1f ), glm::mix( b, m, 0.1f ), color );
							DEBUG_draw_lines->draw( glm::mix( b, m, 0.1f ), glm::mix( c, m, 0.1f ), color );
							DEBUG_draw_lines->draw( glm::mix( c, m, 0.1f ), glm::mix( a, m, 0.1f ), color );
						}
					}
				}
			}

			for (auto const &collider : level.mesh_colliders) {
				glm::mat4x3 collider_to_world = collider.transform->make_local_to_world();

				{ //Early discard:
					// check if AABB of collider overlaps AABB of swept sphere:

					//(1) compute bounding box of collider in world space:
					glm::vec3 local_center = 0.5f * (collider.mesh->max + collider.mesh->min);
					glm::vec3 local_radius = 0.5f * (collider.mesh->max - collider.mesh->min);

					glm::vec3 world_min = collider_to_world * glm::vec4(local_center, 1.0f)
						- glm::abs(local_radius.x * collider_to_world[0])
						- glm::abs(local_radius.y * collider_to_world[1])
						- glm::abs(local_radius.z * collider_to_world[2]);
					glm::vec3 world_max = collider_to_world * glm::vec4(local_center, 1.0f)
						+ glm::abs(local_radius.x * collider_to_world[0])
						+ glm::abs(local_radius.y * collider_to_world[1])
						+ glm::abs(local_radius.z * collider_to_world[2]);

					//(2) check if bounding boxes overlap:
					bool can_skip = !collide_AABB_vs_AABB(sphere_sweep_min, sphere_sweep_max, world_min, world_max);

					//draw to indicate result of check:
					if (iter == 0 && DEBUG_draw_lines && DEBUG_show_geometry) {
						DEBUG_draw_lines->draw_box(glm::mat4x3(
							0.5f * (world_max.x - world_min.x), 0.0f, 0.0f,
							0.0f, 0.5f * (world_max.y - world_min.y), 0.0f,
							0.0f, 0.0f, 0.5f * (world_max.z - world_min.z),
							0.5f * (world_max.x+world_min.x), 0.5f * (world_max.y+world_min.y), 0.5f * (world_max.z+world_min.z)
						), (can_skip ? glm::u8vec4(0x88, 0x88, 0x88, 0xff) : glm::u8vec4(0x88, 0x88, 0x00, 0xff) ) );
					}

					if (can_skip) {
						//AABBs do not overlap; skip detailed check:
						continue;
					}
				}

				//Full (all triangles) test:
				assert(collider.mesh->type == GL_TRIANGLES); //only have code for TRIANGLES not other primitive types
				for (GLuint v = 0; v + 2 < collider.mesh->count; v += 3) {
					//get vertex positions from associated positions buffer:
					//  (and transform to world space)
					glm::vec3 a = collider_to_world * glm::vec4(collider.buffer->positions[collider.mesh->start+v+0], 1.0f);
					glm::vec3 b = collider_to_world * glm::vec4(collider.buffer->positions[collider.mesh->start+v+1], 1.0f);
					glm::vec3 c = collider_to_world * glm::vec4(collider.buffer->positions[collider.mesh->start+v+2], 1.0f);
					//check triangle:
					bool did_collide = collide_swept_sphere_vs_triangle(
						sphere_sweep_from, sphere_sweep_to, sphere_radius,
						a,b,c,
						&collision_t, &collision_at, &collision_out);

					if (did_collide) {
						collided = true;
					}

					//draw to indicate result of check:
					if (iter == 0 && DEBUG_draw_lines) {
						glm::u8vec4 color = (did_collide ? glm::u8vec4(0x88, 0x00, 0x00, 0xff) : glm::u8vec4(0x88, 0x88, 0x00, 0xff));
						if (DEBUG_show_geometry || (did_collide && DEBUG_show_collision)) {
							DEBUG_draw_lines->draw(a,b,color);
							DEBUG_draw_lines->draw(b,c,color);
							DEBUG_draw_lines->draw(c,a,color);
						}
						//do a bit more to highlight colliding triangles (otherwise edges can be over-drawn by non-colliding triangles):
						if (did_collide && DEBUG_show_collision) {
							glm::vec3 m = (a + b + c) / 3.0f;
							DEBUG_draw_lines->draw(glm::mix(a,m,0.1f),glm::mix(b,m,0.1f),color);
							DEBUG_draw_lines->draw(glm::mix(b,m,0.1f),glm::mix(c,m,0.1f),color);
							DEBUG_draw_lines->draw(glm::mix(c,m,0.1f),glm::mix(a,m,0.1f),color);
						}
					}
				}
			}

			if (!collided) {
				position = sphere_sweep_to;
				remain = 0.0f;
				break;
			} else {
				position = glm::mix(sphere_sweep_from, sphere_sweep_to, collision_t);
				float d = glm::dot(velocity, collision_out);
				if (d < 0.0f) {
					velocity -= (1.1f * d) * collision_out;

					//update rotational velocity to reflect relative motion:
					glm::vec3 slip = glm::cross(rotational_velocity, collision_at - position) + velocity;
					//DEBUG: std::cout << "slip: " << slip.x << " " << slip.y << " " << slip.z << std::endl;
					/*if (DEBUG_draw_lines) {
						DEBUG_draw_lines->draw(collision_at, collision_at + slip, glm::u8vec4(0x00, 0xff, 0x00, 0xff));
					}*/
					glm::vec3 change = glm::cross(slip, collision_at - position);
					rotational_velocity += change;
				}
				if (DEBUG_draw_lines && DEBUG_show_collision) {
					//draw a little gadget at the collision point:
					glm::vec3 p1;
					if (std::abs(collision_out.x) <= std::abs(collision_out.y) && std::abs(collision_out.x) <= std::abs(collision_out.z)) {
						p1 = glm::vec3(1.0f, 0.0f, 0.0f);
					} else if (std::abs(collision_out.y) <= std::abs(collision_out.z)) {
						p1 = glm::vec3(0.0f, 1.0f, 0.0f);
					} else {
						p1 = glm::vec3(0.0f, 0.0f, 1.0f);
					}
					p1 = glm::normalize(p1 - glm::dot(p1, collision_out)*collision_out);
					glm::vec3 p2 = glm::cross(collision_out, p1);

					float r = 0.25f;
					glm::u8vec4 color = glm::u8vec4(0xff, 0x00, 0x00, 0xff);
					DEBUG_draw_lines->draw(collision_at + r*p1, collision_at + r*p2, color);
					DEBUG_draw_lines->draw(collision_at + r*p2, collision_at - r*p1, color);
					DEBUG_draw_lines->draw(collision_at - r*p1, collision_at - r*p2, color);
					DEBUG_draw_lines->draw(collision_at - r*p2, collision_at + r*p1, color);
					DEBUG_draw_lines->draw(collision_at, collision_at + collision_out, color);
				}
				/*if (iter == 0) {
					std::cout << collision_out.x << ", " << collision_out.y << ", " << collision_out.z
					<< " / " << velocity.x << ", " << velocity.y << ", " << velocity.z << std::endl; //DEBUG
				}*/
				remain = (1.0f - collision_t) * remain;
			}
		}

		//update player rotation (purely cosmetic):
		rotation = glm::normalize(
			  glm::angleAxis(elapsed * rotational_velocity.x, glm::vec3(1.0f, 0.0f, 0.0f))
			* glm::angleAxis(elapsed * rotational_velocity.y, glm::vec3(0.0f, 1.0f, 0.0f))
			* glm::angleAxis(elapsed * rotational_velocity.z, glm::vec3(0.0f, 0.0f, 1.0f))
			* rotation
		);
	}

	//goal update:
	for (auto &goal : level.goals) {
		goal.spin_acc += elapsed / 10.0f;
		goal.spin_acc -= std::floor(goal.spin_acc);
		//goal.transform->rotation = glm::angleAxis(goal.spin_acc * 2.0f * 3.1415926f, glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));
	}

	{ //camera update:
		level.camera->transform->rotation =
			glm::angleAxis( level.player.view_azimuth, glm::vec3(0.0f, 0.0f, 1.0f) )
			* glm::angleAxis(-level.player.view_elevation + 0.5f * 3.1415926f, glm::vec3(1.0f, 0.0f, 0.0f) )
		;
		glm::vec3 in = level.camera->transform->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		level.camera->transform->position = level.player.transform->position - 7.0f * in - 0.1f * glm::length(level.player.velocity ) * in;
	}
}

void FlyMode::draw(glm::uvec2 const &drawable_size) {
	//--- actual drawing ---
	glClearColor(1.0f, 0.7f, 0.5f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	level.camera->aspect = drawable_size.x / float(drawable_size.y);
	level.draw(*level.camera);

	{ //help text overlay:
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		DrawSprites draw(*font_atlas, glm::vec2(0,0), glm::vec2(320, 200), drawable_size, DrawSprites::AlignSloppy);

		float speed_mod = ( glm::mix( 1.0f, 0.4f, glm::length( level.player.velocity ) / speed_full ) );
		float speed_offset = ( glm::mix( 0.0f, 3.0f, glm::length( level.player.velocity ) / speed_full ) );
		draw.draw_text( std::to_string( goalsHit ), glm::vec2( 155.0f + speed_offset, 95.0f + speed_offset ), speed_mod  );
		draw.draw_text( std::to_string( time_run ), glm::vec2( 255.0f, 165.0f ), 1.0f );
		draw.draw_text( "record: " + std::to_string( time_record ), glm::vec2( 255.0f, 180.0f ), 0.5f );

		if( won )
		{
			draw.draw_text( "finish!", glm::vec2( 10.0f, 155.0f), 3.0f );
		}
	}

	if (DEBUG_draw_lines) { //DEBUG drawing:
		//adjust world-to-clip matrix to current camera:
		DEBUG_draw_lines->world_to_clip = level.camera->make_projection() * level.camera->transform->make_world_to_local();
		//delete object (draws in destructor):
		DEBUG_draw_lines.reset();
	}


	GL_ERRORS();
}

void FlyMode::restart() {
	level = start;
	won = false;
	time_run = 0.0f;
	goalsHit = 0;
	for( auto c : level.goal_colliders )
	{
		const_cast<FlyLevel::GoalCollider&>( c ).isCollided = false;
	}
}
