#include <iostream>
#include "quiz.h"

using namespace std;

int main() {
    int choice;

    do {
        cout << "\n================================\n";
        cout << "  DSA INTERACTIVE QUIZ SYSTEM\n";
        cout << "================================\n";
        cout << "1. Student\n";
        cout << "2. Admin\n";
        cout << "3. Exit\n";
        cout << "Enter your choice: ";

        cin >> choice;

        switch (choice) {
            case 1:
                cout << "\n--- Student Menu ---\n";
                cout << "Welcome, Student!\n";
                startQuiz();
                break;

            case 2:
                cout << "\n--- Admin Menu ---\n";
                cout << "Welcome, Admin!\n";
                displayQuestions();
                break;

            case 3:
                cout << "\nThank you for using the system!\n";
                break;

            default:
                cout << "\nInvalid choice. Please enter 1, 2, or 3.\n";
        }

    } while (choice != 3);

    return 0;
}
