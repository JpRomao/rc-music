#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Library {
    FT_Library handle = nullptr;
    Library() { require(FT_Init_FreeType(&handle) == 0, "FreeType initialization failed"); }
    ~Library() { FT_Done_FreeType(handle); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
};

struct Face {
    FT_Face handle = nullptr;
    Face(FT_Library library, const std::vector<unsigned char>& bytes) {
        require(FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &handle) == 0,
                "The pinned variable font could not be opened");
    }
    ~Face() { FT_Done_Face(handle); }
    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;
};

struct Variation {
    FT_Library library;
    FT_MM_Var* handle = nullptr;
    Variation(FT_Library owner, FT_Face face) : library(owner) {
        require(FT_Get_MM_Var(face, &handle) == 0, "Variation metadata is unavailable");
    }
    ~Variation() { FT_Done_MM_Var(library, handle); }
    Variation(const Variation&) = delete;
    Variation& operator=(const Variation&) = delete;
};

void checkCoordinates(const char* fixture, std::string_view state, FT_UInt count) {
    std::ifstream input(fixture, std::ios::binary);
    require(input.good(), "Cannot open the pinned fixture");
    const std::vector<unsigned char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(font.size() == 28528, "Unexpected fixture size");

    Library library;
    FT_Int major = 0, minor = 0, patch = 0;
    FT_Library_Version(library.handle, &major, &minor, &patch);
    // Qt's attribution says 2.14.3, but this pinned vendor header says 2.14.2.
    // Keep source/runtime identity distinct from the supplier's inventory.
    static_assert(FREETYPE_MAJOR == 2 && FREETYPE_MINOR == 14 && FREETYPE_PATCH == 2);
    require(major == FREETYPE_MAJOR && minor == FREETYPE_MINOR && patch == FREETYPE_PATCH,
            "Runtime version differs from the pinned vendor header");
    Face face(library.handle, font);
    require(FT_HAS_MULTIPLE_MASTERS(face.handle), "The fixture must expose variation axes");
    Variation variation(library.handle, face.handle);
    require(variation.handle->num_axis == 1, "The fixture must have exactly one axis");
    const auto& axis = variation.handle->axis[0];
    constexpr FT_Fixed defaultWeight = 400 * 65536;
    constexpr FT_Fixed customWeight = 650 * 65536;
    require(axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't'), "Unexpected axis tag");
    require(axis.minimum == defaultWeight && axis.def == defaultWeight && axis.maximum == 900 * 65536,
            "Unexpected axis range/default");

    FT_Fixed expected = defaultWeight;
    if (state != "fresh") {
        expected = state == "custom" ? customWeight : defaultWeight;
        auto selected = expected;
        require(FT_Set_Var_Design_Coordinates(face.handle, 1, &selected) == 0, "Cannot select the requested instance");
    }

    // A real public-API conformance check; no malformed font or private state.
    // The caller owns the complete output buffer and checks both guard values.
    constexpr FT_Fixed untouched = 0x5a5a5a5a;
    std::array<FT_Fixed, 4> output{untouched, untouched, untouched, untouched};
    require(FT_Get_Var_Design_Coordinates(face.handle, count, output.data() + 1) == 0,
            "Reading design coordinates failed");
    require(output.front() == untouched && output.back() == untouched, "Output guards changed");
    for (FT_UInt index = 0; index < count; ++index) {
        require(output[index + 1] == (index == 0 ? expected : 0),
                index == 0 ? "The axis coordinate differs from the selected instance"
                           : "Excess coordinate is not zero");
    }
    for (FT_UInt index = count; index < 2; ++index) {
        require(output[index + 1] == untouched, "Values beyond the requested count changed");
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4 ||
        (std::string_view(argv[2]) != "fresh" && std::string_view(argv[2]) != "default" &&
         std::string_view(argv[2]) != "custom") ||
        std::string_view(argv[3]).size() != 1 || argv[3][0] < '0' || argv[3][0] > '2') {
        std::cerr << "Usage: m0_freetype_coordinates <pinned-font> <fresh|default|custom> <0|1|2>\n";
        return 2;
    }
    try {
        checkCoordinates(argv[1], argv[2], static_cast<FT_UInt>(argv[3][0] - '0'));
        std::cout << "PASS coordinates." << argv[2] << '.' << argv[3] << '\n';
        return 0;
    } catch (const std::exception& error) {
        // Do not print coordinate memory, file contents, or absolute paths.
        std::cerr << "FAIL coordinates." << argv[2] << '.' << argv[3] << ": " << error.what() << '\n';
        return 1;
    }
}
