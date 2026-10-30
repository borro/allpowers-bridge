// Offline Arduino boundary for the real CMake/BuildFirmware graph regression.
// It produces all compile outputs, records the actual staged input inventory,
// and never discovers or opens devices.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs = std::filesystem;
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "version") {
    std::cout << "{\"VersionString\":\"1.5.1\"}";
    return 0;
  }
  fs::path output, library, sketch;
  for (int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if (arg=="core" && i+1<argc && std::string(argv[i+1])=="list") {
      std::cout << "{\"platforms\":[{\"id\":\"esp32:esp32\",\"installed_version\":\"3.3.11\"}]}";
      return 0;
    }
    if (arg=="--output-dir" && i+1<argc) output=argv[++i];
    else if (arg=="--library" && i+1<argc) library=argv[++i];
  }
  if (argc) sketch=argv[argc-1];
  if (output.empty() || !fs::is_directory(sketch)) return 2;
  fs::create_directories(output);
  fs::copy_file(PARTITION_FIXTURE,output/"R2500Adapter.ino.partitions.bin",fs::copy_options::overwrite_existing);
  for (const auto *name:{"R2500Adapter.ino.bin","R2500Adapter.ino.bootloader.bin"})
    std::ofstream(output/name,std::ios::binary)<<"offline graph fixture ALLPOWERS_HISTORY_R3_W3_S32"<<'\0';
  std::ofstream inputs(output/"compile-inputs.txt");
  for (const auto &root:{sketch,library})
    for (const auto &entry:fs::recursive_directory_iterator(root))
      if(entry.is_regular_file()) inputs<<fs::relative(entry.path(),root).generic_string()<<'\n';
  std::ofstream(output.parent_path()/(output.filename().string()+"-calls.txt"),std::ios::app)<<"compile\n";
}
