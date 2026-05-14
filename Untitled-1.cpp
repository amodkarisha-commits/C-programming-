#include <iostream>
#include <string>
#include <cctype>
#include <stdexcept>

using namespace std;

class Parser {
private:
    string expr;
    int pos;

    // Skip spaces
    void skipSpaces() {
        while (pos < expr.length() && isspace(expr[pos])) {
            pos++;
        }
    }

    // Parse numbers
    double parseNumber() {
        skipSpaces();

        double number = 0;
        bool hasDecimal = false;
        double decimalPlace = 0.1;

        while (pos < expr.length() &&
              (isdigit(expr[pos]) || expr[pos] == '.')) {

            if (expr[pos] == '.') {
                if (hasDecimal)
                    throw runtime_error("Invalid number format");
                hasDecimal = true;
            }
            else {
                if (!hasDecimal) {
                    number = number * 10 + (expr[pos] - '0');
                } else {
                    number += (expr[pos] - '0') * decimalPlace;
                    decimalPlace /= 10;
                }
            }

            pos++;
        }

        return number;
    }

    // Parse factor: number or (expression)
    double parseFactor() {
        skipSpaces();

        if (pos < expr.length() && expr[pos] == '(') {
            pos++; // skip '('
            double result = parseExpression();

            skipSpaces();
            if (pos >= expr.length() || expr[pos] != ')') {
                throw runtime_error("Missing closing parenthesis");
            }

            pos++; // skip ')'
            return result;
        }

        // Handle negative numbers
        if (pos < expr.length() && expr[pos] == '-') {
            pos++;
            return -parseFactor();
        }

        return parseNumber();
    }

    // Parse term: factor (* or / factor)
    double parseTerm() {
        double result = parseFactor();

        while (true) {
            skipSpaces();

            if (pos < expr.length() && expr[pos] == '*') {
                pos++;
                result *= parseFactor();
            }
            else if (pos < expr.length() && expr[pos] == '/') {
                pos++;
                double divisor = parseFactor();

                if (divisor == 0) {
                    throw runtime_error("Division by zero");
                }

                result /= divisor;
            }
            else {
                break;
            }
        }

        return result;
    }

    // Parse expression: term (+ or - term)
    double parseExpression() {
        double result = parseTerm();

        while (true) {
            skipSpaces();

            if (pos < expr.length() && expr[pos] == '+') {
                pos++;
                result += parseTerm();
            }
            else if (pos < expr.length() && expr[pos] == '-') {
                pos++;
                result -= parseTerm();
            }
            else {
                break;
            }
        }

        return result;
    }

public:
    Parser(string input) {
        expr = input;
        pos = 0;
    }

    double evaluate() {
        double result = parseExpression();

        skipSpaces();

        if (pos != expr.length()) {
            throw runtime_error("Unexpected character found");
        }

        return result;
    }
};

int main() {
    string input;

    cout << "===== Simple Arithmetic Expression Compiler =====\n";
    cout << "Supported Operators: +, -, *, /, ()\n";

    while (true) {
        cout << "\nEnter expression (or type 'exit'): ";
        getline(cin, input);

        if (input == "exit")
            break;

        try {
            Parser parser(input);
            double result = parser.evaluate();

            cout << "Result = " << result << endl;
        }
        catch (exception &e) {
            cout << "Error: " << e.what() << endl;
        }
    }

    return 0;
}