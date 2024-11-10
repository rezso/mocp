#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>

using namespace std;
using namespace std::filesystem;

int main(int argc, char **argv) {
	int min, max;
	path dir, cwd, file;
	ifstream ratings_file;
	string line;

	if (argc != 3 && argc != 4) {
		cerr << "Usage: ratings_find min [max] dir\n";
		exit(EXIT_FAILURE);
	}

	try {
		if (argc == 3) {
			min = stoi(argv[1]);
			max = 5;
			dir = argv[2];
		} else {
			min = stoi(argv[1]);
			max = stoi(argv[2]);
			dir = argv[3];
		}
	}
	catch(...) {
		min = -1;
	}

	if (min < 0 || min > 5 || max < 0 || max > 5) {
		cerr << "min and max should be numbers between 0 and 5.\n";
		exit(EXIT_FAILURE);
	}

	if (!is_directory(dir)) {
		cerr << "Invalid directory.\n";
		exit(EXIT_FAILURE);
	}

	dir = canonical(dir);

	for (auto f : recursive_directory_iterator(dir, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied))
	if (f.path().filename() == "ratings") {
			ratings_file.open(f.path().c_str());
			cwd = f.path().parent_path();

			while (getline(ratings_file, line)) {
				if (line.size() > 3 && line[0] >= '0' && line[0] <= '5' && line[1] == ' ') {
					file = cwd / line.substr(2);
					int n = line[0] - '0';
					if (n >= min && n <= max && is_regular_file(file))
						cout << file.c_str() << '\n';
				}
			}
			ratings_file.close();
		}
}
