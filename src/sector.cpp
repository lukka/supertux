//  $Id$
//
//  SuperTux -  A Jump'n Run
//  Copyright (C) 2004 Matthias Braun <matze@braunis.de
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include <config.h>

#include <memory>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <stdexcept>

#include "app/globals.h"
#include "sector.h"
#include "utils/lispreader.h"
#include "object/gameobjs.h"
#include "object/camera.h"
#include "object/background.h"
#include "object/particlesystem.h"
#include "object/tilemap.h"
#include "tile.h"
#include "audio/sound_manager.h"
#include "gameloop.h"
#include "resources.h"
#include "statistics.h"
#include "special/collision.h"
#include "math/rectangle.h"
#include "math/aatriangle.h"
#include "object/coin.h"
#include "object/block.h"
#include "object/invisible_block.h"
#include "object/platform.h"
#include "object/bullet.h"
#include "badguy/jumpy.h"
#include "badguy/snowball.h"
#include "badguy/bouncing_snowball.h"
#include "badguy/flame.h"
#include "badguy/mriceblock.h"
#include "badguy/mrbomb.h"
#include "badguy/dispenser.h"
#include "badguy/spike.h"
#include "badguy/nolok_01.h"
#include "trigger/door.h"
#include "trigger/sequence_trigger.h"
#include "trigger/secretarea_trigger.h"

Sector* Sector::_current = 0;

Sector::Sector()
  : gravity(10), player(0), solids(0), camera(0),
    currentmusic(LEVEL_MUSIC)
{
  song_title = "Mortimers_chipdisko.mod";
  player = new Player();
  add_object(player);
}

Sector::~Sector()
{
  update_game_objects();
  assert(gameobjects_new.size() == 0);

  for(GameObjects::iterator i = gameobjects.begin(); i != gameobjects.end();
      ++i) {
    delete *i;
  }

  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i)
    delete *i;
    
  if(_current == this)
    _current = 0;
}

GameObject*
Sector::parse_object(const std::string& name, LispReader& reader)
{
  if(name == "background") {
    return new Background(reader);
  } else if(name == "camera") {
    Camera* camera = new Camera(this);
    camera->parse(reader);
    return camera;
  } else if(name == "tilemap") {
    return  new TileMap(reader);
  } else if(name == "particles-snow") {
    SnowParticleSystem* partsys = new SnowParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "particles-clouds") {
    CloudParticleSystem* partsys = new CloudParticleSystem();
    partsys->parse(reader);
    return partsys;
  } else if(name == "door") {
    return new Door(reader);
  } else if(name == "secretarea") {
    return new SecretAreaTrigger(reader);
  } else if(name == "platform") {
    return new Platform(reader);
  } else if(name == "jumpy" || name == "money") {
    return new Jumpy(reader);
  } else if(name == "snowball") {
    return new SnowBall(reader);
  } else if(name == "bouncingsnowball") {
    return new BouncingSnowball(reader);
  } else if(name == "flame") {
    return new Flame(reader);
  } else if(name == "mriceblock") {
    return new MrIceBlock(reader);
  } else if(name == "mrbomb") {
    return new MrBomb(reader);
  } else if(name == "dispenser") {
    return new Dispenser(reader);
  } else if(name == "spike") {
    return new Spike(reader);
  } else if(name == "nolok_01") {
    return new Nolok_01(reader);
  }

  std::cerr << "Unknown object type '" << name << "'.\n";
  return 0;
}

void
Sector::parse(LispReader& lispreader)
{
  _current = this;
  
  for(lisp_object_t* cur = lispreader.get_lisp(); !lisp_nil_p(cur);
      cur = lisp_cdr(cur)) {
    std::string token = lisp_symbol(lisp_car(lisp_car(cur)));
    // FIXME: doesn't handle empty data
    lisp_object_t* data = lisp_car(lisp_cdr(lisp_car(cur)));
    LispReader reader(lisp_cdr(lisp_car(cur)));

    if(token == "name") {
      name = lisp_string(data);
    } else if(token == "gravity") {
      gravity = lisp_real(data);
    } else if(token == "music") {
      song_title = lisp_string(data);
      load_music();
    } else if(token == "spawn-points") {
      SpawnPoint* sp = new SpawnPoint;
      reader.read_string("name", sp->name);
      reader.read_float("x", sp->pos.x);
      reader.read_float("y", sp->pos.y);
      spawnpoints.push_back(sp);
    } else {
      GameObject* object = parse_object(token, reader);
      if(object) {
        add_object(object);
      }
    }
  }

  update_game_objects();
  fix_old_tiles();
  update_game_objects();
  if(!camera) {
    std::cerr << "sector '" << name << "' does not contain a camera.\n";
    camera = new Camera(this);
    add_object(camera);
  }
  if(!solids)
    throw std::runtime_error("sector does not contain a solid tile layer.");
}

void
Sector::parse_old_format(LispReader& reader)
{
  _current = this;
  
  name = "main";
  reader.read_float("gravity", gravity);

  std::string backgroundimage;
  reader.read_string("background", backgroundimage);
  float bgspeed = .5;
  reader.read_float("bkgd_speed", bgspeed);
  bgspeed /= 100;

  Color bkgd_top, bkgd_bottom;
  int r = 0, g = 0, b = 128;
  reader.read_int("bkgd_red_top", r);
  reader.read_int("bkgd_green_top",  g);
  reader.read_int("bkgd_blue_top",  b);
  bkgd_top.red = r;
  bkgd_top.green = g;
  bkgd_top.blue = b;
  
  reader.read_int("bkgd_red_bottom",  r);
  reader.read_int("bkgd_green_bottom", g);
  reader.read_int("bkgd_blue_bottom", b);
  bkgd_bottom.red = r;
  bkgd_bottom.green = g;
  bkgd_bottom.blue = b;
  
  if(backgroundimage != "") {
    Background* background = new Background;
    background->set_image(backgroundimage, bgspeed);
    add_object(background);
  } else {
    Background* background = new Background;
    background->set_gradient(bkgd_top, bkgd_bottom);
    add_object(background);
  }

  std::string particlesystem;
  reader.read_string("particle_system", particlesystem);
  if(particlesystem == "clouds")
    add_object(new CloudParticleSystem());
  else if(particlesystem == "snow")
    add_object(new SnowParticleSystem());

  Vector startpos(100, 170);
  reader.read_float("start_pos_x", startpos.x);
  reader.read_float("start_pos_y", startpos.y);

  SpawnPoint* spawn = new SpawnPoint;
  spawn->pos = startpos;
  spawn->name = "main";
  spawnpoints.push_back(spawn);

  song_title = "Mortimers_chipdisko.mod";
  reader.read_string("music", song_title);
  load_music();

  int width, height = 15;
  reader.read_int("width", width);
  reader.read_int("height", height);
  
  std::vector<unsigned int> tiles;
  if(reader.read_int_vector("interactive-tm", tiles)
      || reader.read_int_vector("tilemap", tiles)) {
    TileMap* tilemap = new TileMap();
    tilemap->set(width, height, tiles, LAYER_TILES, true);
    add_object(tilemap);
  }

  if(reader.read_int_vector("background-tm", tiles)) {
    TileMap* tilemap = new TileMap();
    tilemap->set(width, height, tiles, LAYER_BACKGROUNDTILES, false);
    add_object(tilemap);
  }

  if(reader.read_int_vector("foreground-tm", tiles)) {
    TileMap* tilemap = new TileMap();
    tilemap->set(width, height, tiles, LAYER_FOREGROUNDTILES, false);
    add_object(tilemap);
  }

  // read reset-points (now spawn-points)
  {
    lisp_object_t* cur = 0;
    if(reader.read_lisp("reset-points", cur)) {
      while(!lisp_nil_p(cur)) {
        lisp_object_t* data = lisp_car(cur);
        LispReader reader(lisp_cdr(data));

        Vector sp_pos;
        if(reader.read_float("x", sp_pos.x) && reader.read_float("y", sp_pos.y))
          {
          SpawnPoint* sp = new SpawnPoint;
          sp->name = "main";
          sp->pos = sp_pos;
          spawnpoints.push_back(sp);
          }
                                                             
        cur = lisp_cdr(cur);
      }
    }
  }

  // read objects
  {
    lisp_object_t* cur = 0;
    if(reader.read_lisp("objects", cur)) {
      while(!lisp_nil_p(cur)) {
        lisp_object_t* data = lisp_car(cur);
        std::string object_type = lisp_symbol(lisp_car(data));
                                                                                
        LispReader reader(lisp_cdr(data));

        GameObject* object = parse_object(object_type, reader);
        if(object) {
          add_object(object);
        } else {
          std::cerr << "Unknown object '" << object_type << "' in level.\n";
        }
                                                                               
        cur = lisp_cdr(cur);
      }
    }
  }

  // add a camera
  Camera* camera = new Camera(this);
  add_object(camera);

  update_game_objects();
  fix_old_tiles();
  update_game_objects();
  if(solids == 0)
    throw std::runtime_error("sector does not contain a solid tile layer.");  
}

void
Sector::fix_old_tiles()
{
  // hack for now...
  for(size_t x=0; x < solids->get_width(); ++x) {
    for(size_t y=0; y < solids->get_height(); ++y) {
      const Tile* tile = solids->get_tile(x, y);
      Vector pos(x*32, y*32);
      
      if(tile->id == 112) {
        add_object(new InvisibleBlock(pos));
        solids->change(x, y, 0);
      } else if(tile->id == 295) {
        add_object(new Spike(pos, Spike::NORTH));
        solids->change(x, y, 0);
      } else if(tile->id == 296) {
        add_object(new Spike(pos, Spike::EAST));
        solids->change(x, y, 0);
      } else if(tile->id == 297) {
        add_object(new Spike(pos, Spike::SOUTH));
        solids->change(x, y, 0);
      } else if(tile->id == 298) {
        add_object(new Spike(pos, Spike::WEST));
        solids->change(x, y, 0);
      } else if(tile->attributes & Tile::COIN) {
        add_object(new Coin(pos));
        solids->change(x, y, 0);
      } else if(tile->attributes & Tile::FULLBOX) {
        add_object(new BonusBlock(pos, tile->data));
        solids->change(x, y, 0);
      } else if(tile->attributes & Tile::BRICK) {
        add_object(new Brick(pos, tile->data));
        solids->change(x, y, 0);
      } else if(tile->attributes & Tile::GOAL) {
        add_object(new SequenceTrigger(pos, "endsequence"));
        solids->change(x, y, 0);
      }
    }                                                   
  }
}

void
Sector::write(LispWriter& writer)
{
  writer.write_string("name", name);
  writer.write_float("gravity", gravity);
  writer.write_string("music", song_title);

  // write spawnpoints
  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i) {
    SpawnPoint* spawn = *i;
    writer.start_list("spawn-points");
    writer.write_string("name", spawn->name);
    writer.write_float("x", spawn->pos.x);
    writer.write_float("y", spawn->pos.y);
    writer.end_list("spawn-points");
  }

  // write objects
  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    Serializable* serializable = dynamic_cast<Serializable*> (*i);
    if(serializable)
      serializable->write(writer);
  }
}

void
Sector::add_object(GameObject* object)
{
  // make sure the object isn't already in the list
#ifdef DEBUG
  for(GameObjects::iterator i = gameobjects.begin(); i != gameobjects.end();
      ++i) {
    if(*i == object) {
      assert("object already added to sector" == 0);
    }
  }
  for(GameObjects::iterator i = gameobjects_new.begin();
      i != gameobjects_new.end(); ++i) {
    if(*i == object) {
      assert("object already added to sector" == 0);
    }
  }
#endif

  gameobjects_new.push_back(object);
}

void
Sector::activate(const std::string& spawnpoint)
{
  _current = this;

  // Apply bonuses from former levels
  switch (player_status.bonus)
    {
    case PlayerStatus::NO_BONUS:
      break;
                                                                                
    case PlayerStatus::FLOWER_BONUS:
      player->got_power = Player::FIRE_POWER;  // FIXME: add ice power to here
      // fall through
                                                                                
    case PlayerStatus::GROWUP_BONUS:
      player->grow(false);
      break;
    }

  SpawnPoint* sp = 0;
  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i) {
    if((*i)->name == spawnpoint) {
      sp = *i;
      break;
    }
  }
  if(!sp) {
    std::cerr << "Spawnpoint '" << spawnpoint << "' not found.\n";
  } else {
    player->move(sp->pos);
  }

  camera->reset(player->get_pos());
}

Vector
Sector::get_best_spawn_point(Vector pos)
{
  Vector best_reset_point = Vector(-1,-1);

  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i) {
    if((*i)->name != "main")
      continue;
    if((*i)->pos.x > best_reset_point.x && (*i)->pos.x < pos.x)
      best_reset_point = (*i)->pos;
  }

  return best_reset_point;
}

void
Sector::action(float elapsed_time)
{
  player->check_bounds(camera);
                                                                                
  /* update objects */
  for(GameObjects::iterator i = gameobjects.begin();
          i != gameobjects.end(); ++i) {
    GameObject* object = *i;
    if(!object->is_valid())
      continue;
    
    object->action(elapsed_time);
  }
                                                                                
  /* Handle all possible collisions. */
  collision_handler();                                                                              
  update_game_objects();
}

void
Sector::update_game_objects()
{
  /** cleanup marked objects */
  for(std::vector<GameObject*>::iterator i = gameobjects.begin();
      i != gameobjects.end(); /* nothing */) {
    if((*i)->is_valid() == false) {
      Bullet* bullet = dynamic_cast<Bullet*> (*i);
      if(bullet) {
        bullets.erase(
            std::remove(bullets.begin(), bullets.end(), bullet),
            bullets.end());
      }
      delete *i;
      i = gameobjects.erase(i);
    } else {
      ++i;
    }
  }

  /* add newly created objects */
  for(std::vector<GameObject*>::iterator i = gameobjects_new.begin();
      i != gameobjects_new.end(); ++i)
  {
    Bullet* bullet = dynamic_cast<Bullet*> (*i);
    if(bullet)
      bullets.push_back(bullet);

    TileMap* tilemap = dynamic_cast<TileMap*> (*i);
    if(tilemap && tilemap->is_solid()) {
      if(solids == 0) {
        solids = tilemap;
      } else {
        std::cerr << "Another solid tilemaps added. Ignoring.";
      }
    }

    Camera* camera = dynamic_cast<Camera*> (*i);
    if(camera) {
      if(this->camera != 0) {
        std::cerr << "Warning: Multiple cameras added. Ignoring.";
        continue;
      }
      this->camera = camera;
    }

    gameobjects.push_back(*i);
  }
  gameobjects_new.clear();
}

void
Sector::draw(DrawingContext& context)
{
  context.push_transform();
  context.set_translation(camera->get_translation());
  
  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    GameObject* object = *i; 
    if(!object->is_valid())
      continue;
    
    object->draw(context);
  }

  context.pop_transform();
}

static const float DELTA = .001;

void
Sector::collision_tilemap(MovingObject* object, int depth)
{
  if(depth >= 4) {
#ifdef DEBUG
    std::cout << "Max collision depth reached.\n";
#endif
    object->movement = Vector(0, 0);
    return;
  }

  // calculate rectangle where the object will move
  float x1, x2;
  if(object->get_movement().x >= 0) {
    x1 = object->get_pos().x;
    x2 = object->get_bbox().p2.x + object->get_movement().x;
  } else {
    x1 = object->get_pos().x + object->get_movement().x;
    x2 = object->get_bbox().p2.x;
  }
  float y1, y2;
  if(object->get_movement().y >= 0) {
    y1 = object->get_pos().y;
    y2 = object->get_bbox().p2.y + object->get_movement().y;
  } else {
    y1 = object->get_pos().y + object->get_movement().y;
    y2 = object->get_bbox().p2.y;
  }

  // test with all tiles in this rectangle
  int starttilex = int(x1-1) / 32;
  int starttiley = int(y1-1) / 32;
  int max_x = int(x2+1);
  int max_y = int(y2+1);

  CollisionHit temphit, hit;
  Rectangle dest = object->get_bbox();
  dest.move(object->movement);
  hit.time = -1; // represents an invalid value
  for(int x = starttilex; x*32 < max_x; ++x) {
    for(int y = starttiley; y*32 < max_y; ++y) {
      const Tile* tile = solids->get_tile(x, y);
      if(!tile)
        continue;
      if(!(tile->attributes & Tile::SOLID))
        continue;
      if((tile->attributes & Tile::UNISOLID) && object->movement.y < 0)
        continue;

      if(tile->attributes & Tile::SLOPE) { // slope tile
        AATriangle triangle;
        Vector p1(x*32, y*32);
        Vector p2((x+1)*32, (y+1)*32);
        triangle = AATriangle(p1, p2, tile->data);

        if(Collision::rectangle_aatriangle(temphit, dest, object->movement,
              triangle)) {
          if(temphit.time > hit.time)
            hit = temphit;
        }
      } else { // normal rectangular tile
        Rectangle rect(x*32, y*32, (x+1)*32, (y+1)*32);
        if(Collision::rectangle_rectangle(temphit, dest,
              object->movement, rect)) {
          if(temphit.time > hit.time)
            hit = temphit;
        }
      }
    }
  }

  // did we collide at all?
  if(hit.time < 0)
    return;
 
  // call collision function
  HitResponse response = object->collision(*solids, hit);
  if(response == ABORT_MOVE) {
    object->movement = Vector(0, 0);
    return;
  }
  if(response == FORCE_MOVE) {
      return;
  }
  // move out of collision and try again
  object->movement += hit.normal * (hit.depth + DELTA);
  collision_tilemap(object, depth+1);
}

void
Sector::collision_object(MovingObject* object1, MovingObject* object2)
{
  CollisionHit hit;
  Rectangle dest1 = object1->get_bbox();
  dest1.move(object1->get_movement());
  Rectangle dest2 = object2->get_bbox();
  dest2.move(object2->get_movement());

  Vector movement = object1->get_movement() - object2->get_movement();
  if(Collision::rectangle_rectangle(hit, dest1, movement, dest2)) {
    HitResponse response1 = object1->collision(*object2, hit);
    hit.normal *= -1;
    HitResponse response2 = object2->collision(*object1, hit);

    if(response1 != CONTINUE) {
      if(response1 == ABORT_MOVE)
        object1->movement = Vector(0, 0);
      if(response2 == CONTINUE)
        object2->movement += hit.normal * (hit.depth + DELTA);
    } else if(response2 != CONTINUE) {
      if(response2 == ABORT_MOVE)
        object2->movement = Vector(0, 0);
      if(response1 == CONTINUE)
        object1->movement += -hit.normal * (hit.depth + DELTA);
    } else {
      object1->movement += -hit.normal * (hit.depth/2 + DELTA);
      object2->movement += hit.normal * (hit.depth/2 + DELTA);
    }
  }
}

void
Sector::collision_handler()
{
  for(std::vector<GameObject*>::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    GameObject* gameobject = *i;
    if(!gameobject->is_valid())
      continue;
    MovingObject* movingobject = dynamic_cast<MovingObject*> (gameobject);
    if(!movingobject)
      continue;
    if(movingobject->get_flags() & GameObject::FLAG_NO_COLLDET) {
      movingobject->bbox.move(movingobject->movement);
      movingobject->movement = Vector(0, 0);
      continue;
    }

    // collision with tilemap
    if(! (movingobject->movement == Vector(0, 0)))
      collision_tilemap(movingobject, 0);

    // collision with other objects
    for(std::vector<GameObject*>::iterator i2 = i+1;
        i2 != gameobjects.end(); ++i2) {
      GameObject* other_object = *i2;
      if(!other_object->is_valid() 
          || other_object->get_flags() & GameObject::FLAG_NO_COLLDET)
        continue;
      MovingObject* movingobject2 = dynamic_cast<MovingObject*> (other_object);
      if(!movingobject2)
        continue;

      collision_object(movingobject, movingobject2);
    }

    movingobject->bbox.move(movingobject->get_movement());
    movingobject->movement = Vector(0, 0);
  }
}

bool
Sector::add_bullet(const Vector& pos, float xm, Direction dir)
{
  if(player->got_power == Player::FIRE_POWER) {
    if(bullets.size() > MAX_FIRE_BULLETS-1)
      return false;
  } else if(player->got_power == Player::ICE_POWER) {
    if(bullets.size() > MAX_ICE_BULLETS-1)
      return false;
  }
                                                                                
  Bullet* new_bullet = 0;
  if(player->got_power == Player::FIRE_POWER)
    new_bullet = new Bullet(pos, xm, dir, FIRE_BULLET);
  else if(player->got_power == Player::ICE_POWER)
    new_bullet = new Bullet(pos, xm, dir, ICE_BULLET);
  else
    throw std::runtime_error("wrong bullet type.");
  add_object(new_bullet);

  SoundManager::get()->play_sound(IDToSound(SND_SHOOT));

  return true;
}

bool
Sector::add_smoke_cloud(const Vector& pos)
{
  add_object(new SmokeCloud(pos));
  return true;
}

void
Sector::add_floating_text(const Vector& pos, const std::string& text)
{
  add_object(new FloatingText(pos, text));
}

void
Sector::load_music()
{
  char* song_path;
  char* song_subtitle;
                                                                                
  level_song = SoundManager::get()->load_music(datadir + "/music/" + song_title);
                                                                                
  song_path = (char *) malloc(sizeof(char) * datadir.length() +
                              strlen(song_title.c_str()) + 8 + 5);
  song_subtitle = strdup(song_title.c_str());
  strcpy(strstr(song_subtitle, "."), "\0");
  sprintf(song_path, "%s/music/%s-fast%s", datadir.c_str(),
          song_subtitle, strstr(song_title.c_str(), "."));
  if(!SoundManager::get()->exists_music(song_path)) {
    level_song_fast = level_song;
  } else {
    level_song_fast = SoundManager::get()->load_music(song_path);
  }
  free(song_subtitle);
  free(song_path);
}

void
Sector::play_music(int type)
{
  currentmusic = type;
  switch(currentmusic) {
    case HURRYUP_MUSIC:
      SoundManager::get()->play_music(level_song_fast);
      break;
    case LEVEL_MUSIC:
      SoundManager::get()->play_music(level_song);
      break;
    case HERRING_MUSIC:
      SoundManager::get()->play_music(herring_song);
      break;
    default:
      SoundManager::get()->halt_music();
      break;
  }
}

int
Sector::get_music_type()
{
  return currentmusic;
}

int
Sector::get_total_badguys()
{
  int total_badguys = 0;
#if 0
  for(GameObjects::iterator i = gameobjects_new.begin(); i != gameobjects_new.end(); ++i)
    {
    BadGuy* badguy = dynamic_cast<BadGuy*> (*i);
    if(badguy)
      total_badguys++;
    }
#endif
  return total_badguys;
}

bool
Sector::inside(const Rectangle& rect) const
{
  if(rect.p1.x > solids->get_width() * 32 
      || rect.p1.y > solids->get_height() * 32
      || rect.p2.x < 0 || rect.p2.y < 0)
    return false;

  return true;
}
