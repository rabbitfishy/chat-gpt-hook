#include "includes.h"

Aimbot g_aimbot{ };;

void AimPlayer::UpdateAnimations( LagRecord *record ) {
	CCSGOPlayerAnimState *state = m_player->m_PlayerAnimState( );
	if ( !state )
		return;

	// player respawned.
	if ( m_player->m_flSpawnTime( ) != m_spawn ) {
		// reset animation state.
		game::ResetAnimationState( state );

		// note new spawn time.
		m_spawn = m_player->m_flSpawnTime( );
	}

	// backup curtime.
	float curtime = g_csgo.m_globals->m_curtime;
	float frametime = g_csgo.m_globals->m_frametime;

	// set curtime to animtime.
	// set frametime to ipt just like on the server during simulation.
	g_csgo.m_globals->m_curtime = record->m_anim_time;
	g_csgo.m_globals->m_frametime = g_csgo.m_globals->m_interval;

	// backup stuff that we do not want to fuck with.
	AnimationBackup_t backup;

	backup.m_origin = m_player->m_vecOrigin( );
	backup.m_abs_origin = m_player->GetAbsOrigin( );
	backup.m_velocity = m_player->m_vecVelocity( );
	backup.m_abs_velocity = m_player->m_vecAbsVelocity( );
	backup.m_flags = m_player->m_fFlags( );
	backup.m_eflags = m_player->m_iEFlags( );
	backup.m_duck = m_player->m_flDuckAmount( );
	backup.m_body = m_player->m_flLowerBodyYawTarget( );
	m_player->GetAnimLayers( backup.m_layers );

	// is player a bot?
	bool bot = game::IsFakePlayer( m_player->index( ) );

	// reset fakewalk state.
	record->m_fake_walk = false;
	record->m_mode = Resolver::Modes::RESOLVE_NONE;

	// Fix velocity
	// https://github.com/VSES/SourceEngine2007/blob/master/se2007/game/client/c_baseplayer.cpp#L659
	if ( record->m_lag > 0 && record->m_lag < 16 ) {
		if ( m_records.size( ) >= 2 ) {
			// Get pointer to previous record
			LagRecord* previous = m_records[ 1 ].get( );
			if ( previous && !previous->dormant( ) ) {
				float elapsedTime = game::TICKS_TO_TIME( record->m_lag );
				record->m_velocity = ( record->m_origin - previous->m_origin ) * ( 1.f / elapsedTime );
			}
		}
	}

	// set this fucker, it will get overriden.
	record->m_anim_velocity = record->m_velocity;

	// fix various issues with the game eW91dHViZS5jb20vZHlsYW5ob29r
	// these issues can only occur when a player is choking data.
	if ( record->m_lag > 1 && !bot ) {
		// detect fakewalk.
		float speed = record->m_velocity.length( );

		if ( record->m_flags & FL_ONGROUND ) {
			// Check if player is not moving much and their weight is not changing much
			if ( speed > 0.1f && speed < 100.f && fabs( record->m_layers[ 6 ].m_weight ) < 0.1f ) {
				record->m_fake_walk = true;
			}
		}

		if ( record->m_fake_walk )
			record->m_anim_velocity = record->m_velocity = { 0.f, 0.f, 0.f };

		if ( m_records.size( ) < 2 )
			return;

		LagRecord* previous = m_records[ 1 ].get( );

		if ( !previous || previous->dormant( ) )
			return;

		m_player->m_fFlags( ) = previous->m_flags & ~FL_ONGROUND;

		if ( record->m_flags & FL_ONGROUND && previous->m_flags & FL_ONGROUND )
			m_player->m_fFlags( ) |= FL_ONGROUND;

		if ( record->m_layers[ 4 ].m_weight != 1.f && previous->m_layers[ 4 ].m_weight == 1.f && record->m_layers[ 5 ].m_weight != 0.f )
			m_player->m_fFlags( ) |= FL_ONGROUND;

		if ( record->m_flags & FL_ONGROUND && !( previous->m_flags & FL_ONGROUND ) )
			m_player->m_fFlags( ) &= ~FL_ONGROUND;

		float duck = record->m_duck - previous->m_duck;
		float time = record->m_sim_time - previous->m_sim_time;
		float change = ( duck / time ) * g_csgo.m_globals->m_interval;

		m_player->m_flDuckAmount( ) = previous->m_duck + change;

		if ( record->m_fake_walk ) {
			record->m_anim_velocity = { 0.f, 0.f, 0.f };
		}
		else {
			vec3_t velo = record->m_velocity - previous->m_velocity;
			vec3_t accel = ( velo / time ) * g_csgo.m_globals->m_interval;
			record->m_anim_velocity = previous->m_velocity + accel;
		}

	}

	bool fake = !bot && g_menu.main.aimbot.correct.get( );

	// if using fake angles, correct angles.
	if ( fake )
		g_resolver.ResolveAngles( m_player, record );

	// set stuff before animating.
	m_player->m_vecOrigin( ) = record->m_origin;
	m_player->m_vecVelocity( ) = m_player->m_vecAbsVelocity( ) = record->m_anim_velocity;
	m_player->m_flLowerBodyYawTarget( ) = record->m_body;

	// EFL_DIRTY_ABSVELOCITY
	// skip call to C_BaseEntity::CalcAbsoluteVelocity
	m_player->m_iEFlags( ) &= ~0x1000;

	// write potentially resolved angles.
	m_player->m_angEyeAngles( ) = record->m_eye_angles;

	// fix animating in same frame.
	if ( state->m_frame == g_csgo.m_globals->m_frame )
		state->m_frame -= 1;

	// 'm_animating' returns true if being called from SetupVelocity, passes raw velocity to animstate.
	m_player->m_bClientSideAnimation( ) = true;
	m_player->UpdateClientSideAnimation( );
	m_player->m_bClientSideAnimation( ) = false;

	// correct poses if fake angles.
	if ( fake )
		g_resolver.ResolvePoses( m_player, record );

	// store updated/animated poses and rotation in lagrecord.
	m_player->GetPoseParameters( record->m_poses );
	record->m_abs_ang = m_player->GetAbsAngles( );

	// restore backup data.
	m_player->m_vecOrigin( ) = backup.m_origin;
	m_player->m_vecVelocity( ) = backup.m_velocity;
	m_player->m_vecAbsVelocity( ) = backup.m_abs_velocity;
	m_player->m_fFlags( ) = backup.m_flags;
	m_player->m_iEFlags( ) = backup.m_eflags;
	m_player->m_flDuckAmount( ) = backup.m_duck;
	m_player->m_flLowerBodyYawTarget( ) = backup.m_body;
	m_player->SetAbsOrigin( backup.m_abs_origin );
	m_player->SetAnimLayers( backup.m_layers );

	// IMPORTANT: do not restore poses here, since we want to preserve them for rendering.
	// also dont restore the render angles which indicate the model rotation.

	// restore globals.
	g_csgo.m_globals->m_curtime = curtime;
	g_csgo.m_globals->m_frametime = frametime;
}

void AimPlayer::OnNetUpdate( Player* player ) {
	// Check if aimbot should be disabled
	if ( !g_menu.main.aimbot.enable.get( ) || player->m_lifeState( ) == LIFE_DEAD || !player->enemy( g_cl.m_local ) ) {
		player->m_bClientSideAnimation( ) = true;
		m_records.clear( );
		return;
	}

	// Check if client is processing
	if ( !g_cl.m_processing ) {
		player->m_bClientSideAnimation( ) = true;
		return;
	}

	// Clear lag records if player has changed
	if ( m_player != player ) {
		m_records.clear( );
	}

	// Update player pointer
	m_player = player;

	// Insert dormancy separator if player is dormant
	if ( player->dormant( ) ) {
		if ( !m_records.empty( ) && m_records.front( ).get( )->dormant( ) ) {
			return;
		}
		m_records.emplace_front( std::make_shared<LagRecord>( player ) );
		LagRecord* current = m_records.front( ).get( );
		current->m_dormant = true;
	}

	// Check if new data update is required
	bool update = m_records.empty( ) || player->m_flSimulationTime( ) > m_records.front( ).get( )->m_sim_time;
	if ( !update && !player->dormant( ) && player->m_vecOrigin( ) != player->m_vecOldOrigin( ) ) {
		update = true;
		player->m_flSimulationTime( ) = game::TICKS_TO_TIME( g_csgo.m_cl->m_server_tick );
	}

	// Add new data update
	if ( update ) {
		m_records.emplace_front( std::make_shared<LagRecord>( player ) );
		LagRecord* current = m_records.front( ).get( );
		current->m_dormant = false;
		UpdateAnimations( current );
		g_bones.setup( m_player, nullptr, current );
	}

	// Limit the amount of stored data
	while ( m_records.size( ) > 256 ) {
		m_records.pop_back( );
	}
}

void AimPlayer::OnRoundStart( Player *player ) {
	m_player = player;
	m_walk_record = LagRecord{ };
	m_shots = 0;
	m_missed_shots = 0;

	// reset stand and body index.
	m_stand_index  = 0;
	m_stand_index2 = 0;
	m_body_index   = 0;

	m_moved = false;

	m_records.clear( );
	m_hitboxes.clear( );

	// IMPORTANT: DO NOT CLEAR LAST HIT SHIT.
}

void AimPlayer::SetupHitboxes( LagRecord *record, bool history ) {
	// reset hitboxes.
	m_hitboxes.clear( );

	if ( g_cl.m_weapon_id == ZEUS ) {
		// hitboxes for the zeus.
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );
		return;
	}

	// prefer, always.
	if ( g_menu.main.aimbot.baim1.get( 0 ) )
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );

	// prefer, lethal.
	if ( g_menu.main.aimbot.baim1.get( 1 ) )
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::LETHAL } );

	// prefer, lethal x2.
	if ( g_menu.main.aimbot.baim1.get( 2 ) )
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::LETHAL2 } );

	// prefer, fake.
	if ( g_menu.main.aimbot.baim1.get( 3 ) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK )
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );

	// prefer, in air.
	if ( g_menu.main.aimbot.baim1.get( 4 ) && !( record->m_pred_flags & FL_ONGROUND ) )
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );

	bool only{ false };

	// only, always.
	if ( g_menu.main.aimbot.baim2.get( 0 ) ) {
		only = true;
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );
	}

	// only, health.
	if ( g_menu.main.aimbot.baim2.get( 1 ) && m_player->m_iHealth( ) <= ( int )g_menu.main.aimbot.baim_hp.get( ) ) {
		only = true;
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );
	}

	// only, fake.
	if ( g_menu.main.aimbot.baim2.get( 2 ) && record->m_mode != Resolver::Modes::RESOLVE_NONE && record->m_mode != Resolver::Modes::RESOLVE_WALK ) {
		only = true;
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );
	}

	// only, in air.
	if ( g_menu.main.aimbot.baim2.get( 3 ) && !( record->m_pred_flags & FL_ONGROUND ) ) {
		only = true;
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );
	}

	// only, on key.
	if ( g_input.GetKeyState( g_menu.main.aimbot.baim_key.get( ) ) ) {
		only = true;
		m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::PREFER } );
	}

	// only baim conditions have been met.
	// do not insert more hitboxes.
	if ( only )
		return;

	std::vector< size_t > hitbox{ history ? g_menu.main.aimbot.hitbox_history.GetActiveIndices( ) : g_menu.main.aimbot.hitbox.GetActiveIndices( ) };
	if ( hitbox.empty( ) )
		return;

	for ( const auto &h : hitbox ) {
		// head.
		if ( h == 0 )
			m_hitboxes.push_back( { HITBOX_HEAD, HitscanMode::NORMAL } );

		// chest.
		if ( h == 1 ) {
			m_hitboxes.push_back( { HITBOX_THORAX, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_CHEST, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_UPPER_CHEST, HitscanMode::NORMAL } );
		}

		// stomach.
		if ( h == 2 ) {
			m_hitboxes.push_back( { HITBOX_PELVIS, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_BODY, HitscanMode::NORMAL } );
		}

		// arms.
		if ( h == 3 ) {
			m_hitboxes.push_back( { HITBOX_L_UPPER_ARM, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_R_UPPER_ARM, HitscanMode::NORMAL } );
		}

		// legs.
		if ( h == 4 ) {
			m_hitboxes.push_back( { HITBOX_L_THIGH, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_R_THIGH, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_L_CALF, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_R_CALF, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_L_FOOT, HitscanMode::NORMAL } );
			m_hitboxes.push_back( { HITBOX_R_FOOT, HitscanMode::NORMAL } );
		}
	}
}

void Aimbot::init( ) {
	// clear old targets.
	m_targets.clear( );

	m_target = nullptr;
	m_aim = vec3_t{ };
	m_angle = ang_t{ };
	m_damage = 0.f;
	m_record = nullptr;
	m_stop = false;

	m_best_dist = std::numeric_limits< float >::max( );
	m_best_fov = 180.f + 1.f;
	m_best_damage = 0.f;
	m_best_hp = 100 + 1;
	m_best_lag = std::numeric_limits< float >::max( );
	m_best_height = std::numeric_limits< float >::max( );
}

void Aimbot::StripAttack( ) {
	if ( g_cl.m_weapon_id == REVOLVER )
		g_cl.m_cmd->m_buttons &= ~IN_ATTACK2;

	else
		g_cl.m_cmd->m_buttons &= ~IN_ATTACK;
}

void Aimbot::think( ) {
	// Do all startup routines.
	init( );

	// Check if the player has a weapon.
	if ( !g_cl.m_weapon ) return;

	// Do not aimbot for grenades or bomb.
	if ( g_cl.m_weapon_type == WEAPONTYPE_GRENADE || g_cl.m_weapon_type == WEAPONTYPE_C4 ) return;

	// Strip attack if the weapon is not firing.
	if ( !g_cl.m_weapon_fire ) StripAttack( );

	// Return if aimbot is not enabled.
	if ( !g_menu.main.aimbot.enable.get( ) ) return;

	// Check if the weapon is a revolver and if it is cocking.
	bool is_revolver = g_cl.m_weapon_id == REVOLVER && g_cl.m_revolver_cock != 0;

	// If the revolver is cocking, choke the previous tick.
	if ( is_revolver && g_cl.m_revolver_cock > 0 && g_cl.m_revolver_cock == g_cl.m_revolver_query ) {
		*g_cl.m_packet = false;
		return;
	}

	// If the weapon is firing and not lagging, choke this tick.
	if ( g_cl.m_weapon_fire && !g_cl.m_lag && !is_revolver ) {
		*g_cl.m_packet = false;
		StripAttack( );
		return;
	}

	// Return if the weapon cannot fire this tick.
	if ( !g_cl.m_weapon_fire ) return;

	// Store valid targets in the `m_targets` vector.
	for ( int i = 1; i <= g_csgo.m_globals->m_max_clients; ++i ) {
		Player* player = g_csgo.m_entlist->GetClientEntity<Player*>( i );
		if ( !IsValidTarget( player ) ) continue;

		AimPlayer* data = &m_players[ i - 1 ];
		if ( !data ) continue;

		// Store player as potential target this tick.
		m_targets.emplace_back( data );
	}

	// If the weapon is a knife, run knifebot if enabled.
	if ( g_cl.m_weapon_type == WEAPONTYPE_KNIFE && g_cl.m_weapon_id != ZEUS ) {
		if ( g_menu.main.aimbot.knifebot.get( ) ) knife( );
		return;
	}

	// Find the target if there are any available.
	find( );

	// Apply aimbot.
	apply( );
}


void Aimbot::find( ) {
	struct BestTarget_t { Player *player; vec3_t pos; float damage; LagRecord *record; };

	vec3_t       tmp_pos;
	float        tmp_damage;
	BestTarget_t best;
	best.player = nullptr;
	best.damage = -1.f;
	best.pos = vec3_t{ };
	best.record = nullptr;

	if ( m_targets.empty( ) )
		return;

	if ( g_cl.m_weapon_id == ZEUS && !g_menu.main.aimbot.zeusbot.get( ) )
		return;

	// iterate all targets.
	for ( const auto &t : m_targets ) {
		if ( t->m_records.empty( ) )
			continue;

		// this player broke lagcomp.
		// his bones have been resetup by our lagcomp.
		// therfore now only the front record is valid.
		if ( g_menu.main.aimbot.lagfix.get( ) && g_lagcomp.StartPrediction( t ) ) {
			LagRecord *front = t->m_records.front( ).get( );

			t->SetupHitboxes( front, false );
			if ( t->m_hitboxes.empty( ) )
				continue;

			// rip something went wrong..
			if ( t->GetBestAimPosition( tmp_pos, tmp_damage, front ) && SelectTarget( front, tmp_pos, tmp_damage ) ) {

				// if we made it so far, set shit.
				best.player = t->m_player;
				best.pos = tmp_pos;
				best.damage = tmp_damage;
				best.record = front;
			}
		}

		// player did not break lagcomp.
		// history aim is possible at this point.
		else {
			LagRecord *ideal = g_resolver.FindIdealRecord( t );
			if ( !ideal )
				continue;

			t->SetupHitboxes( ideal, false );
			if ( t->m_hitboxes.empty( ) )
				continue;

			// try to select best record as target.
			if ( t->GetBestAimPosition( tmp_pos, tmp_damage, ideal ) && SelectTarget( ideal, tmp_pos, tmp_damage ) ) {
				// if we made it so far, set shit.
				best.player = t->m_player;
				best.pos = tmp_pos;
				best.damage = tmp_damage;
				best.record = ideal;
			}

			LagRecord *last = g_resolver.FindLastRecord( t );
			if ( !last || last == ideal )
				continue;

			t->SetupHitboxes( last, true );
			if ( t->m_hitboxes.empty( ) )
				continue;

			// rip something went wrong..
			if ( t->GetBestAimPosition( tmp_pos, tmp_damage, last ) && SelectTarget( last, tmp_pos, tmp_damage ) ) {
				// if we made it so far, set shit.
				best.player = t->m_player;
				best.pos = tmp_pos;
				best.damage = tmp_damage;
				best.record = last;
			}
		}
	}

	// verify our target and set needed data.
	if ( best.player && best.record ) {
		// calculate aim angle.
		math::VectorAngles( best.pos - g_cl.m_shoot_pos, m_angle );

		// set member vars.
		m_target = best.player;
		m_aim = best.pos;
		m_damage = best.damage;
		m_record = best.record;

		// write data, needed for traces / etc.
		m_record->cache( );

		// set autostop shit.
		m_stop = !( g_cl.m_buttons & IN_JUMP );

		bool on = g_menu.main.aimbot.hitchance.get( ) && g_menu.main.config.mode.get( ) == 0;
		bool hit = on && CheckHitchance( m_target, m_angle );

		// if we can scope.
		bool can_scope = !g_cl.m_local->m_bIsScoped( ) && ( g_cl.m_weapon_id == AUG || g_cl.m_weapon_id == SG553 || g_cl.m_weapon_type == WEAPONTYPE_SNIPER_RIFLE );

		if ( can_scope ) {
			// always.
			if ( g_menu.main.aimbot.zoom.get( ) == 1 ) {
				g_cl.m_cmd->m_buttons |= IN_ATTACK2;
				return;
			}

			// hitchance fail.
			else if ( g_menu.main.aimbot.zoom.get( ) == 2 && on && !hit ) {
				g_cl.m_cmd->m_buttons |= IN_ATTACK2;
				return;
			}
		}

		if ( hit || !on ) {
			// right click attack.
			if ( g_menu.main.config.mode.get( ) == 1 && g_cl.m_weapon_id == REVOLVER )
				g_cl.m_cmd->m_buttons |= IN_ATTACK2;

			// left click attack.
			else
				g_cl.m_cmd->m_buttons |= IN_ATTACK;
		}
	}
}

bool Aimbot::CheckHitchance( Player* player, const ang_t& angle ) {
	constexpr float HITCHANCE_MAX = 100.f;
	constexpr int SEED_MAX = 255;

	vec3_t start{ g_cl.m_shoot_pos };
	vec3_t end, fwd, right, up, dir, wep_spread;
	float inaccuracy, spread;
	CGameTrace tr;
	size_t total_hits = 0, needed_hits = static_cast< size_t >( std::ceil( ( g_menu.main.aimbot.hitchance_amount.get( ) * SEED_MAX ) / HITCHANCE_MAX ) );

	// Get the needed directional vectors
	math::AngleVectors( angle, &fwd, &right, &up );

	// Store off the inaccuracy and spread (these functions can be intensive, so we only call them once)
	inaccuracy = g_cl.m_weapon->GetInaccuracy( );
	spread = g_cl.m_weapon->GetSpread( );

	// Get end of trace
	end = start + ( fwd * g_cl.m_weapon_info->m_range );

	// Iterate all possible seeds
	for ( int i = 0; i <= SEED_MAX; i++ ) {
		// Get spread
		wep_spread = g_cl.m_weapon->CalculateSpread( i, inaccuracy, spread );

		// Get spread direction
		dir = ( fwd + ( right * wep_spread.x ) + ( up * wep_spread.y ) ).normalized( );

		// Set up the ray and trace
		g_csgo.m_engine_trace->ClipRayToEntity( Ray( start, end + ( dir * g_cl.m_weapon_info->m_range ) ), MASK_SHOT, player, &tr );

		// Check if we hit a valid player/hitgroup and increment the total hits
		if ( tr.m_entity == player && game::IsValidHitgroup( tr.m_hitgroup ) )
			total_hits++;

		// We made it
		if ( total_hits >= needed_hits )
			return true;

		// We can't make it anymore
		if ( ( SEED_MAX - i + total_hits ) < needed_hits )
			return false;
	}

	return false;
}

bool AimPlayer::SetupHitboxPoints( LagRecord *record, BoneArray *bones, int index, std::vector< vec3_t > &points ) {
	// reset points.
	points.clear( );

	const model_t *model = m_player->GetModel( );
	if ( !model )
		return false;

	studiohdr_t *hdr = g_csgo.m_model_info->GetStudioModel( model );
	if ( !hdr )
		return false;

	mstudiohitboxset_t *set = hdr->GetHitboxSet( m_player->m_nHitboxSet( ) );
	if ( !set )
		return false;

	mstudiobbox_t *bbox = set->GetHitbox( index );
	if ( !bbox )
		return false;

	// get hitbox scales.
	float scale = g_menu.main.aimbot.scale.get( ) / 100.f;

	// big inair fix.
	if ( !( record->m_pred_flags & FL_ONGROUND ) )
		scale = 0.7f;

	float bscale = g_menu.main.aimbot.body_scale.get( ) / 100.f;

	// these indexes represent boxes.
	if ( bbox->m_radius <= 0.f ) {
		// references: 
		//      https://developer.valvesoftware.com/wiki/Rotation_Tutorial
		//      CBaseAnimating::GetHitboxBonePosition
		//      CBaseAnimating::DrawServerHitboxes

		// convert rotation angle to a matrix.
		matrix3x4_t rot_matrix;
		g_csgo.AngleMatrix( bbox->m_angle, rot_matrix );

		// apply the rotation to the entity input space (local).
		matrix3x4_t matrix;
		math::ConcatTransforms( bones[ bbox->m_bone ], rot_matrix, matrix );

		// extract origin from matrix.
		vec3_t origin = matrix.GetOrigin( );

		// compute raw center point.
		vec3_t center = ( bbox->m_mins + bbox->m_maxs ) / 2.f;

		// the feet hiboxes have a side, heel and the toe.
		const int HITBOX_RIGHT_FOOT = HITBOX_R_FOOT;
		const int HITBOX_LEFT_FOOT = HITBOX_L_FOOT;

		if ( index == HITBOX_RIGHT_FOOT || index == HITBOX_LEFT_FOOT ) {
			// Foot offset relative to center of hitbox 
			// (bbox->m_mins.z - center.z) * 0.875f
			float foot_offset = ( bbox->m_mins.z - center.z ) * 0.875f;

			// Invert offset for left foot
			if ( index == HITBOX_LEFT_FOOT )
				foot_offset *= -1.f;

			// Center of foot
			points.push_back( { center.x, center.y, center.z + foot_offset } );

			if ( g_menu.main.aimbot.multipoint.get( 3 ) ) {
				// Heel offset relative to center of hitbox
				// (bbox->m_mins.x - center.x) * scale
				float heel_offset = ( bbox->m_mins.x - center.x );
				if ( scale != 0 )
					heel_offset *= scale;

				// Toe offset relative to center of hitbox
				// (bbox->m_maxs.x - center.x) * scale
				float toe_offset = ( bbox->m_maxs.x - center.x );
				if ( scale != 0 )
					toe_offset *= scale;

				// Heel
				points.push_back( { center.x + heel_offset, center.y, center.z } );

				// Toe
				points.push_back( { center.x + toe_offset, center.y, center.z } );
			}
		}

		// nothing to do here we are done.
		if ( points.empty( ) )
			return false;

		// rotate our bbox points by their correct angle
		// and convert our points to world space.
		for ( auto &p : points ) {
			// VectorRotate.
			// rotate point by angle stored in matrix.
			p = { p.dot( matrix[ 0 ] ), p.dot( matrix[ 1 ] ), p.dot( matrix[ 2 ] ) };

			// transform point to world space.
			p += origin;
		}
	}

	// these hitboxes are capsules.
	else {
		float r = bbox->m_radius * scale;
		float br = bbox->m_radius * bscale;

		vec3_t center = ( bbox->m_mins + bbox->m_maxs ) / 2.f;

		if ( index == HITBOX_HEAD ) {
			points.push_back( center );

			if ( g_menu.main.aimbot.multipoint.get( 0 ) ) {
				constexpr float rotation = 0.70710678f;
				points.push_back( { bbox->m_maxs.x + ( rotation * r ), bbox->m_maxs.y + ( -rotation * r ), bbox->m_maxs.z } );
				points.push_back( { bbox->m_maxs.x, bbox->m_maxs.y, bbox->m_maxs.z + r } );
				points.push_back( { bbox->m_maxs.x, bbox->m_maxs.y, bbox->m_maxs.z - r } );
				points.push_back( { bbox->m_maxs.x, bbox->m_maxs.y - r, bbox->m_maxs.z } );

				CCSGOPlayerAnimState* state = record->m_player->m_PlayerAnimState( );
				if ( state && record->m_anim_velocity.length( ) <= 0.1f && record->m_eye_angles.x <= state->m_min_pitch )
					points.push_back( { bbox->m_maxs.x - r, bbox->m_maxs.y, bbox->m_maxs.z } );
			}
		}
		else if ( index == HITBOX_BODY ) {
			points.push_back( center );
			if ( g_menu.main.aimbot.multipoint.get( 2 ) )
				points.push_back( { center.x, bbox->m_maxs.y - br, center.z } );
		}
		else if ( index == HITBOX_PELVIS || index == HITBOX_UPPER_CHEST ) {
			points.push_back( { center.x, bbox->m_maxs.y - r, center.z } );
		}
		else if ( index == HITBOX_THORAX || index == HITBOX_CHEST ) {
			points.push_back( center );

			if ( g_menu.main.aimbot.multipoint.get( 1 ) ) {
				points.push_back( { center.x, bbox->m_maxs.y - r, center.z } );
			}
		}
		else if ( index == HITBOX_R_CALF || index == HITBOX_L_CALF ) {
			points.push_back( center );

			if ( g_menu.main.aimbot.multipoint.get( 3 ) ) {
				points.push_back( { bbox->m_maxs.x - ( bbox->m_radius / 2.f ), bbox->m_maxs.y, bbox->m_maxs.z } );
			}
		}
		else if ( index == HITBOX_R_THIGH || index == HITBOX_L_THIGH ) {
			points.push_back( center );
		}
		else if ( index == HITBOX_R_UPPER_ARM || index == HITBOX_L_UPPER_ARM ) {
			points.push_back( { bbox->m_maxs.x + bbox->m_radius, center.y, center.z } );
		}
		else {
			return false;
		}

		if ( points.empty( ) ) {
			return false;
		}

		for ( auto& p : points ) {
			math::VectorTransform( p, bones[ bbox->m_bone ], p );
		}
	}

	return true;
}

bool AimPlayer::GetBestAimPosition( vec3_t &aim, float &damage, LagRecord *record ) {
	bool                  done, pen;
	float                 dmg, pendmg;
	HitscanData_t         scan;
	std::vector< vec3_t > points;

	// get player hp.
	int hp = std::min( 100, m_player->m_iHealth( ) );

	if ( g_cl.m_weapon_id == ZEUS ) {
		dmg = pendmg = hp;
		pen = true;
	}

	else {
		dmg = g_menu.main.aimbot.minimal_damage.get( );
		if ( g_menu.main.aimbot.minimal_damage_hp.get( ) )
			dmg = std::ceil( ( dmg / 100.f ) * hp );

		pendmg = g_menu.main.aimbot.penetrate_minimal_damage.get( );
		if ( g_menu.main.aimbot.penetrate_minimal_damage_hp.get( ) )
			pendmg = std::ceil( ( pendmg / 100.f ) * hp );

		pen = g_menu.main.aimbot.penetrate.get( );
	}

	// write all data of this record l0l.
	record->cache( );

	// iterate hitboxes.
	for ( const auto& hitbox : m_hitboxes ) {
		done = false;

		// Setup points on hitbox
		if ( !SetupHitboxPoints( record, record->m_bones, hitbox.m_index, points ) )
			continue;

		// Iterate points on hitbox
		for ( const auto& point : points ) {
			penetration::PenetrationInput_t input;

			input.m_damage = damage;
			input.m_damage_pen = pendmg;
			input.m_can_pen = pen;
			input.m_target = m_player;
			input.m_from = g_cl.m_local;
			input.m_pos = point;

			// Ignore minimum damage
			if ( hitbox.m_mode == HitscanMode::LETHAL || hitbox.m_mode == HitscanMode::LETHAL2 )
				input.m_damage = input.m_damage_pen = 1.f;

			penetration::PenetrationOutput_t output;

			// Check if we can hit the player
			if ( penetration::run( &input, &output ) ) {
				// Ignore hit if not a headshot
				if ( hitbox.m_index == HITBOX_HEAD && output.m_hitgroup != HITGROUP_HEAD )
					continue;

				// Check hitbox selection mode
				if ( hitbox.m_mode == HitscanMode::PREFER ) {
					done = true;
				}
				else if ( hitbox.m_mode == HitscanMode::LETHAL && output.m_damage >= m_player->m_iHealth( ) ) {
					done = true;
				}
				else if ( hitbox.m_mode == HitscanMode::LETHAL2 && ( output.m_damage * 2.f ) >= m_player->m_iHealth( ) ) {
					done = true;
				}
				else if ( hitbox.m_mode == HitscanMode::NORMAL ) {
					if ( output.m_damage > scan.m_damage ) {
						scan.m_damage = output.m_damage;
						scan.m_pos = point;

						// Break if first point is lethal
						if ( point == points.front( ) && output.m_damage >= m_player->m_iHealth( ) )
							break;
					}
				}

				if ( done ) {
					scan.m_damage = output.m_damage;
					scan.m_pos = point;
					break;
				}
			}
		}

		if ( done )
			break;
	}
	// we found something that we can damage.
	// set out vars.
	if ( scan.m_damage > 0.f ) {
		aim = scan.m_pos;
		damage = scan.m_damage;
		return true;
	}

	return false;
}

bool Aimbot::SelectTarget( LagRecord *record, const vec3_t &aim, float damage ) {
	float dist, fov, height;
	int   hp;

	// fov check.
	if ( g_menu.main.aimbot.fov.get( ) ) {
		// if out of fov, retn false.
		if ( math::GetFOV( g_cl.m_view_angles, g_cl.m_shoot_pos, aim ) > g_menu.main.aimbot.fov_amount.get( ) )
			return false;
	}

	switch ( g_menu.main.aimbot.selection.get( ) ) {

		// distance.
	case 0:
		dist = ( record->m_pred_origin - g_cl.m_shoot_pos ).length( );

		if ( dist < m_best_dist ) {
			m_best_dist = dist;
			return true;
		}

		break;

		// crosshair.
	case 1:
		fov = math::GetFOV( g_cl.m_view_angles, g_cl.m_shoot_pos, aim );

		if ( fov < m_best_fov ) {
			m_best_fov = fov;
			return true;
		}

		break;

		// damage.
	case 2:
		if ( damage > m_best_damage ) {
			m_best_damage = damage;
			return true;
		}

		break;

		// lowest hp.
	case 3:
		// fix for retarded servers?
		hp = std::min( 100, record->m_player->m_iHealth( ) );

		if ( hp < m_best_hp ) {
			m_best_hp = hp;
			return true;
		}

		break;

		// least lag.
	case 4:
		if ( record->m_lag < m_best_lag ) {
			m_best_lag = record->m_lag;
			return true;
		}

		break;

		// height.
	case 5:
		height = record->m_pred_origin.z - g_cl.m_local->m_vecOrigin( ).z;

		if ( height < m_best_height ) {
			m_best_height = height;
			return true;
		}

		break;

	default:
		return false;
	}

	return false;
}

void Aimbot::apply( ) {
	bool attack, attack2;

	// attack states.
	attack = ( g_cl.m_cmd->m_buttons & IN_ATTACK );
	attack2 = ( g_cl.m_weapon_id == REVOLVER && g_cl.m_cmd->m_buttons & IN_ATTACK2 );

	// ensure we're attacking.
	if ( attack || attack2 ) {
		// choke every shot.
		*g_cl.m_packet = false;

		if ( m_target ) {
			// make sure to aim at un-interpolated data.
			// do this so BacktrackEntity selects the exact record.
			if ( m_record && !m_record->m_broke_lc )
				g_cl.m_cmd->m_tick = game::TIME_TO_TICKS( m_record->m_sim_time + g_cl.m_lerp );

			// set angles to target.
			g_cl.m_cmd->m_view_angles = m_angle;

			// if not silent aim, apply the viewangles.
			if ( !g_menu.main.aimbot.silent.get( ) )
				g_csgo.m_engine->SetViewAngles( m_angle );

			g_visuals.DrawHitboxMatrix( m_record, colors::white, 10.f );
		}

		// nospread.
		if ( g_menu.main.aimbot.nospread.get( ) && g_menu.main.config.mode.get( ) == 1 )
			NoSpread( );

		// norecoil.
		if ( g_menu.main.aimbot.norecoil.get( ) )
			g_cl.m_cmd->m_view_angles -= g_cl.m_local->m_aimPunchAngle( ) * g_csgo.weapon_recoil_scale->GetFloat( );

		// store fired shot.
		g_shots.OnShotFire( m_target ? m_target : nullptr, m_target ? m_damage : -1.f, g_cl.m_weapon_info->m_bullets, m_target ? m_record : nullptr );

		// set that we fired.
		g_cl.m_shot = true;
	}
}

void Aimbot::NoSpread( ) {
	bool    attack2;
	vec3_t  spread, forward, right, up, dir;

	// revolver state.
	attack2 = ( g_cl.m_weapon_id == REVOLVER && ( g_cl.m_cmd->m_buttons & IN_ATTACK2 ) );

	// get spread.
	spread = g_cl.m_weapon->CalculateSpread( g_cl.m_cmd->m_random_seed, attack2 );

	// compensate.
	g_cl.m_cmd->m_view_angles -= { -math::rad_to_deg( std::atan( spread.length_2d( ) ) ), 0.f, math::rad_to_deg( std::atan2( spread.x, spread.y ) ) };
}