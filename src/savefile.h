#include "./helper.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <logger.hpp>
#include <map.hpp>
#include <nbt/parser.hpp>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;

struct Dimension {
  std::string ns, id;

  Dimension(std::string ns, std::string id) : ns(ns), id(id){};
  Dimension(std::string _id);

  bool operator==(const Dimension &) const;

  fs::path suffix() const;

  std::string to_string() { return fmt::format("{}:{}", ns, id); };
};

struct SaveFile {
  std::string name;
  std::time_t last_played;
  fs::path folder;
  std::vector<Dimension> dimensions;

  SaveFile();
  SaveFile(const fs::path &_folder);

  bool valid() const { return last_played; }
  void getDimensions();

  fs::path region(const Dimension &) const;
  World::Coordinates getWorld(const Dimension &);
};

bool assert_save(const fs::path &root);

void to_json(json &, const Dimension &);
void to_json(json &, const SaveFile &);
