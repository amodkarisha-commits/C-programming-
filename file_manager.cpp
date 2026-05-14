/*
 * ╔══════════════════════════════════════════════════════════╗
 * ║          FILE MANAGEMENT TOOL — C++ Application          ║
 * ║     Demonstrates: Read, Write, Append, and more          ║
 * ╚══════════════════════════════════════════════════════════╝
 *
 * Compile:  g++ -std=c++17 -o file_manager file_manager.cpp
 * Run:      ./file_manager
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <filesystem>
#include <iomanip>
#include <ctime>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────
//  ANSI colour helpers
// ─────────────────────────────────────────────
const std::string RESET  = "\033[0m";
const std::string BOLD   = "\033[1m";
const std::string RED    = "\033[31m";
const std::string GREEN  = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string CYAN   = "\033[36m";
const std::string MAGENTA= "\033[35m";

void printBanner() {
    std::cout << CYAN << BOLD
              << "\n╔══════════════════════════════════════════════╗\n"
              << "║       📁  FILE MANAGEMENT TOOL  📁            ║\n"
              << "║        Powered by C++17 fstream API           ║\n"
              << "╚══════════════════════════════════════════════╝\n"
              << RESET << "\n";
}

void printMenu() {
    std::cout << BOLD << "─── OPERATIONS ───────────────────────────────\n" << RESET
              << "  " << GREEN  << "[1]" << RESET << "  Write    — Create / overwrite a file\n"
              << "  " << YELLOW << "[2]" << RESET << "  Append   — Add content to existing file\n"
              << "  " << CYAN   << "[3]" << RESET << "  Read     — Display file contents\n"
              << "  " << MAGENTA<< "[4]" << RESET << "  Info     — File metadata & stats\n"
              << "  " << GREEN  << "[5]" << RESET << "  Copy     — Duplicate a file\n"
              << "  " << RED    << "[6]" << RESET << "  Delete   — Remove a file\n"
              << "  " << BOLD   << "[7]" << RESET << "  List     — List files in directory\n"
              << "  " << RED    << "[0]" << RESET << "  Exit\n"
              << "──────────────────────────────────────────────\n"
              << BOLD << "Choice: " << RESET;
}

// ─────────────────────────────────────────────
//  1. WRITE (create / overwrite)
// ─────────────────────────────────────────────
void writeFile() {
    std::string filename;
    std::cout << YELLOW << "\nFilename to write: " << RESET;
    std::cin >> filename;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::ofstream ofs(filename);          // std::ios::out | std::ios::trunc (default)
    if (!ofs.is_open()) {
        std::cerr << RED << "✗  Could not open \"" << filename << "\" for writing.\n" << RESET;
        return;
    }

    std::cout << CYAN << "Enter content (type END on a new line to finish):\n" << RESET;
    std::string line;
    std::size_t bytes = 0;
    while (std::getline(std::cin, line) && line != "END") {
        ofs << line << '\n';
        bytes += line.size() + 1;
    }

    ofs.close();
    std::cout << GREEN << "✔  Written " << bytes << " bytes to \"" << filename << "\".\n" << RESET;
}

// ─────────────────────────────────────────────
//  2. APPEND
// ─────────────────────────────────────────────
void appendFile() {
    std::string filename;
    std::cout << YELLOW << "\nFilename to append to: " << RESET;
    std::cin >> filename;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::ofstream ofs(filename, std::ios::app);   // ← append flag
    if (!ofs.is_open()) {
        std::cerr << RED << "✗  Could not open \"" << filename << "\" for appending.\n" << RESET;
        return;
    }

    // Timestamp separator
    std::time_t now = std::time(nullptr);
    ofs << "\n--- Appended on " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")
        << " ---\n";

    std::cout << CYAN << "Enter content to append (type END on a new line to finish):\n" << RESET;
    std::string line;
    std::size_t bytes = 0;
    while (std::getline(std::cin, line) && line != "END") 
        ofs << line << '\n';
        bytes += line.size() + 1;
    }

    ofs.close();
    std::cout << GREEN << "✔  Appended " << bytes << " bytes to \"" << filename << "\".\n" << RESET;
}

// ─────────────────────────────────────────────
//  3. READ
// ─────────────────────────────────────────────
void readFile() {
    std::string filename;
    std::cout << YELLOW << "\nFilename to read: " << RESET;
    std::cin >> filename;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << RED << "✗  Could not open \"" << filename << "\" for reading.\n" << RESET;
        return;
    }

    std::cout << CYAN << "\n──── Contents of \"" << filename << "\" ────\n" << RESET;
    std::string line;
    int lineNum = 0;
    while (std::getline(ifs, line)) {
        std::cout << BOLD << std::setw(4) << ++lineNum << " │ " << RESET << line << '\n';
    }

    if (lineNum == 0)
        std::cout << YELLOW << "  (file is empty)\n" << RESET;

    ifs.close();
    std::cout << CYAN << "──────────────────────────────────────────────\n"
              << lineNum << " line(s) read.\n" << RESET;
}

// ─────────────────────────────────────────────
//  4. FILE INFO / METADATA
// ─────────────────────────────────────────────
void fileInfo() {
    std::string filename;
    std::cout << YELLOW << "\nFilename for info: " << RESET;
    std::cin >> filename;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    fs::path p(filename);
    if (!fs::exists(p)) {
        std::cerr << RED << "✗  File \"" << filename << "\" does not exist.\n" << RESET;
        return;
    }

    auto sz = fs::file_size(p);

    // Count lines & words
    std::ifstream ifs(filename);
    int lines = 0, words = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        ++lines;
        std::istringstream ss(line);
        std::string word;
        while (ss >> word) ++words;
    }
    ifs.close();

    std::cout << CYAN << "\n──── Info: \"" << filename << "\" ────\n" << RESET
              << "  Extension : " << p.extension().string()  << "\n"
              << "  Size      : " << sz     << " bytes\n"
              << "  Lines     : " << lines  << "\n"
              << "  Words     : " << words  << "\n"
              << "  Is regular: " << (fs::is_regular_file(p) ? "yes" : "no") << "\n";
}

// ─────────────────────────────────────────────
//  5. COPY
// ─────────────────────────────────────────────
void copyFile() {
    std::string src, dst;
    std::cout << YELLOW << "\nSource filename      : " << RESET;
    std::cin >> src;
    std::cout << YELLOW << "Destination filename : " << RESET;
    std::cin >> dst;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    try {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        std::cout << GREEN << "✔  Copied \"" << src << "\" → \"" << dst << "\".\n" << RESET;
    } catch (const fs::filesystem_error& e) {
        std::cerr << RED << "✗  " << e.what() << "\n" << RESET;
    }
}

// ─────────────────────────────────────────────
//  6. DELETE
// ─────────────────────────────────────────────
void deleteFile() {
    std::string filename;
    std::cout << YELLOW << "\nFilename to delete: " << RESET;
    std::cin >> filename;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << RED << "Are you sure you want to delete \"" << filename
              << "\"? (yes/no): " << RESET;
    std::string confirm;
    std::getline(std::cin, confirm);

    if (confirm == "yes" || confirm == "y") {
        if (fs::remove(filename))
            std::cout << GREEN << "✔  \"" << filename << "\" deleted.\n" << RESET;
        else
            std::cerr << RED << "✗  File not found or could not be deleted.\n" << RESET;
    } else {
        std::cout << YELLOW << "  Delete cancelled.\n" << RESET;
    }
}

// ─────────────────────────────────────────────
//  7. LIST directory
// ─────────────────────────────────────────────
void listDirectory() {
    std::string dir;
    std::cout << YELLOW << "\nDirectory to list (. for current): " << RESET;
    std::cin >> dir;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << RED << "✗  \"" << dir << "\" is not a valid directory.\n" << RESET;
        return;
    }

    std::cout << CYAN << "\n──── Contents of \"" << dir << "\" ────\n" << RESET;
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string type = entry.is_regular_file() ? "file" : "dir ";
        std::cout << "  [" << type << "]  " << entry.path().filename().string();
        if (entry.is_regular_file())
            std::cout << "  (" << fs::file_size(entry.path()) << " bytes)";
        std::cout << '\n';
        ++count;
    }
    std::cout << CYAN << "──── " << count << " item(s) ────\n" << RESET;
}

// ─────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────
int main() {
    printBanner();

    int choice = -1;
    while (choice != 0) {
        printMenu();
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }

        switch (choice) {
            case 1: writeFile();     break;
            case 2: appendFile();    break;
            case 3: readFile();      break;
            case 4: fileInfo();      break;
            case 5: copyFile();      break;
            case 6: deleteFile();    break;
            case 7: listDirectory(); break;
            case 0:
                std::cout << CYAN << "\nGoodbye! 👋\n" << RESET;
                break;
            default:
                std::cerr << RED << "✗  Invalid choice. Try again.\n" << RESET;
        }
        std::cout << '\n';
    }
    return 0;
}
