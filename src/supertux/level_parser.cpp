//  SuperTux
//  Copyright (C) 2015 Ingo Ruhnke <grumbel@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "supertux/level_parser.hpp"

#include <physfs.h>
#include <sstream>

#include "supertux/level.hpp"
#include "supertux/sector.hpp"
#include "supertux/sector_parser.hpp"
#include "util/log.hpp"
#include "util/reader.hpp"
#include "util/reader_document.hpp"
#include "util/reader_mapping.hpp"

std::string
LevelParser::get_level_name(const std::string& filename)
{
  try
  {
    register_translation_directory(filename);
    auto doc = ReaderDocument::from_file(filename);
    auto root = doc.get_root();

    if (root.get_name() != "supertux-level") {
      return "";
    } else {
      auto mapping = root.get_mapping();
      std::string name;
      mapping.get("name", name);
      return name;
    }
  }
  catch(const std::exception& e)
  {
    log_warning << "Problem getting name of '" << filename << "': "
                << e.what() << std::endl;
    return "";
  }
}

std::unique_ptr<Level>
LevelParser::from_stream(std::istream& stream, const std::string& context, bool editable)
{
  auto level = std::make_unique<Level>();
  LevelParser parser(*level, editable);
  parser.load(stream, context);
  return level;
}

std::unique_ptr<Level>
LevelParser::from_file(const std::string& filename, bool editable)
{
  auto level = std::make_unique<Level>();
  LevelParser parser(*level, editable);
  parser.load(filename);
  return level;
}

std::unique_ptr<Level>
LevelParser::from_nothing(const std::string& basedir)
{
  auto level = std::make_unique<Level>();
  LevelParser parser(*level, false);

  // Find a free level filename
  std::string level_file;
  int num = 0;
  do {
    num++;
    level_file = basedir + "/level" + std::to_string(num) + ".stl";
  } while ( PHYSFS_exists(level_file.c_str()) );
  std::string level_name = "Level " + std::to_string(num);
  level_file = "level" + std::to_string(num) + ".stl";

  parser.create(level_file, level_name, false);
  return level;
}

std::unique_ptr<Level>
LevelParser::from_nothing_worldmap(const std::string& basedir, const std::string& name)
{
  auto level = std::make_unique<Level>();
  LevelParser parser(*level, false);

  // Find a free level filename
  std::string level_file = basedir + "/worldmap.stwm";
  if (PHYSFS_exists(level_file.c_str())) {
    int num = 0;
    do {
      num++;
      level_file = basedir + "/worldmap" + std::to_string(num) + ".stwm";
    } while ( PHYSFS_exists(level_file.c_str()) );
    level_file = "worldmap" + std::to_string(num) + ".stwm";
  } else {
    level_file = "worldmap.stwm";
  }

  parser.create(level_file, name, true);
  return level;
}

LevelParser::LevelParser(Level& level, bool editable) :
  m_level(level),
  m_editable(editable)
{
}

void
LevelParser::load(std::istream& stream, const std::string& context)
{
  auto doc = ReaderDocument::from_stream(stream, context);
  load(doc);
}

void
LevelParser::load(const std::string& filepath)
{
  m_level.m_filename = filepath;
  register_translation_directory(filepath);
  try {
    auto doc = ReaderDocument::from_file(filepath);
    load(doc);
  } catch(std::exception& e) {
    std::stringstream msg;
    msg << "Problem when reading level '" << filepath << "': " << e.what();
    throw std::runtime_error(msg.str());
  }
}

void
LevelParser::load(const ReaderDocument& doc)
{
  auto root = doc.get_root();

  if (root.get_name() != "supertux-level")
    throw std::runtime_error("file is not a supertux-level file.");

  auto level = root.get_mapping();

  int version = 1;
  level.get("version", version);
  if (version == 1) {
    log_info << "[" << doc.get_filename() << "] level uses old format: version 1" << std::endl;
    load_old_format(level);
  } else if (version == 2) {
    level.get("tileset", m_level.m_tileset);

    level.get("name", m_level.m_name);
    level.get("author", m_level.m_author);
    level.get("contact", m_level.m_contact);
    level.get("license", m_level.m_license);
    level.get("target-time", m_level.m_target_time);

    auto iter = level.get_iter();
    while (iter.next()) {
      if (iter.get_key() == "sector") {
        auto sector = SectorParser::from_reader(m_level, iter.as_mapping(), m_editable);
        m_level.add_sector(std::move(sector));
      }
    }

    if (m_level.m_license.empty()) {
      log_warning << "[" <<  doc.get_filename() << "] The level author \"" << m_level.m_author
                  << "\" did not specify a license for this level \""
                  << m_level.m_name << "\". You might not be allowed to share it."
                  << std::endl;
    }
  } else {
    log_warning << "[" << doc.get_filename() << "] level format version " << version << " is not supported" << std::endl;
  }

  m_level.m_stats.init(m_level);
}

void
LevelParser::load_old_format(const ReaderMapping& reader)
{
  reader.get("name", m_level.m_name);
  reader.get("author", m_level.m_author);

  auto sector = SectorParser::from_reader_old_format(m_level, reader, m_editable);
  m_level.add_sector(std::move(sector));
}

void
LevelParser::create(const std::string& filepath, const std::string& levelname, bool worldmap)
{
  m_level.m_filename = filepath;
  m_level.m_name = levelname;
  m_level.m_license = "CC-BY-SA 4.0 International";
  m_level.m_tileset = worldmap ? "images/worldmap.strf" : "images/tiles.strf";

  auto sector = SectorParser::from_nothing(m_level);
  sector->set_name("main");
  m_level.add_sector(std::move(sector));
}

/* EOF */
