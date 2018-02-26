
#include <string>
#include <sstream>
#include <iomanip>

using namespace std;

string url_encode(const string& value)
{
    stringstream escaped;
    escaped.fill('0');
    escaped << hex;

    for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i)
    {
        string::value_type c = (*i);

        // Keep alphanumeric and other accepted characters intact
        if ((c > 32 && c < 127 && isalnum(c)) || c == L'-' || c == L'_' || c == L'.' || c == L'~' || c == L'/' || c == L':' || c == L'?' || c == L'&')
        {
            escaped << static_cast<unsigned char>(c);
            continue;
        }

        // Any other characters are percent-encoded
        escaped << uppercase;
        escaped << '%' << setw(2) << int((unsigned char)c);
        escaped << nouppercase;
    }

    return escaped.str();
}
