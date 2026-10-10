#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include "quiz.h"
#include "question_bank.h"

using namespace std;

struct Option {
    string text;
    bool correct;
};

struct Question {
    string topic;
    string difficulty;
    string text;
    QuestionBank questionBank;
    string codeAnswer;
    string explanation;
    bool isCode;
};

vector<Question> questions;
mt19937 rng(random_device{}());

void addMCQ(string topic, string difficulty,
            string text, vector<string> options,
            int correct, string explanation) {
    Question q;
    q.topic = topic;
    q.difficulty = difficulty;
    q.text = text;
    q.explanation = explanation;
    q.isCode = false;

    for (int i = 0; i < (int)options.size(); i++) {
        q.options.push_back({options[i], i == correct});
    }

    questionBank.addQuestion(q);
}

void addCodeQuestion(string topic, string difficulty,
                     string text, string answer,
                     string explanation) {
    Question q;
    q.topic = topic;
    q.difficulty = difficulty;
    q.text = text;
    q.codeAnswer = answer;
    q.explanation = explanation;
    q.isCode = true;
    questions.push_back(q);
}

void loadQuestions() {
   static bool loaded = false;
    
       if (loaded) return;
        loaded = true;
    
    addMCQ("Stack", "Easy",
        "PUSH(10), PUSH(20), PUSH(30), POP(). "
        "What remains from bottom to top?",
        {"10, 20", "20, 30", "10, 30", "30, 20"},
        0, "POP removes 30, leaving 10 and 20.");

    addMCQ("Queue", "Easy",
        "ENQUEUE(5), ENQUEUE(10), ENQUEUE(15), DEQUEUE(). "
        "What remains from front to rear?",
        {"10, 15", "5, 10", "15, 10", "5, 15"},
        0, "DEQUEUE removes 5.");

    addMCQ("Linked List", "Easy",
        "List: 10 -> 20 -> 30. Insert 15 after 10, "
        "then delete 20. What is the final list?",
        {"10 -> 15 -> 30", "10 -> 20 -> 15 -> 30",
         "15 -> 10 -> 30", "10 -> 30 -> 15"},
        0, "The final list is 10 -> 15 -> 30.");

    addMCQ("Searching", "Medium",
        "Binary search on [2, 4, 6, 8, 10, 12, 14], "
        "using zero-based indices and mid=(low+high)/2. "
        "Which values are checked when searching for 10?",
        {"8, 12, 10", "8, 10", "6, 8, 10", "10, 8, 12"},
        0, "The values checked are 8, then 12, then 10.");

    addMCQ("Sorting", "Medium",
        "One left-to-right pass of Bubble Sort on "
        "[5, 1, 4, 2] in ascending order gives:",
        {"[1, 4, 2, 5]", "[1, 2, 4, 5]",
         "[5, 1, 2, 4]", "[1, 5, 4, 2]"},
        0, "The largest value, 5, moves to the end.");

    addMCQ("Stack", "Medium",
        "PUSH(4), PUSH(7), POP(), PUSH(9), POP(). "
        "What is the top element?",
        {"4", "7", "9", "Empty"},
        0, "The stack contains only 4.");

    addMCQ("Queue", "Hard",
        "A queue contains 10, 20, 30. Remove twice, "
        "then enqueue 40 and 50. What remains?",
        {"30, 40, 50", "10, 20, 30, 40, 50",
         "40, 50, 30", "30, 50, 40"},
        0, "The remaining queue is 30, 40, 50.");

    addCodeQuestion("Linked List", "Hard",
        "Code: int a[]={10,20,30}; int* p=a; "
        "cout << *(p+1); What is the output?",
        "20", "p+1 points to the second array element.");

    addCodeQuestion("Searching", "Medium",
        "Code: int a[]={3,6,9,12}; int i=0; "
        "while(a[i]!=9) i++; cout << i; Output?",
        "2", "The value 9 is at zero-based index 2.");

    addCodeQuestion("Sorting", "Hard",
        "Code: int a[]={3,1,2}; swap(a[0],a[1]); "
        "swap(a[1],a[2]); cout << a[0] << a[1] "
        "<< a[2]; Output?",
        "123", "The array becomes 1, 2, 3.");
}

void displayQuestions() {
    loadQuestions();
    questionBank.displayQuestions();
}


void startQuiz() {
    loadQuestions();

    vector<Question> allQuestions = questionBank.getAllQuestions();

    if (allQuestions.size() < 10) {
        cout << "Not enough questions to start the quiz.\n";
        return;
    }

    vector<int> order(allQuestions.size());

    for (int i = 0; i < (int)order.size(); i++) {
        order[i] = i;
    }

    shuffle(order.begin(), order.end(), rng);

    int score = 0;

    for (int n = 0; n < 10; n++) {
        Question q = allQuestions[order[n]];

        cout << "\nQuestion " << n + 1 << ": "
             << q.text << "\n";

        if (q.isCode) {
            string answer;
            cout << "Enter output: ";
            getline(cin >> ws, answer);

            if (answer == q.codeAnswer) {
                cout << "Correct!\n";
                score++;
            } else {
                cout << "Incorrect. Correct output: "
                     << q.codeAnswer << "\n";
            }
        } else {
            shuffle(q.options.begin(), q.options.end(), rng);

            for (int i = 0; i < (int)q.options.size(); i++) {
                cout << i + 1 << ". "
                     << q.options[i].text << "\n";
            }

            int answer;
            cout << "Choose an option: ";
            cin >> answer;

            if (answer >= 1 &&
                answer <= (int)q.options.size() &&
                q.options[answer - 1].correct) {
                cout << "Correct!\n";
                score++;
            } else {
                cout << "Incorrect.\n";
            }
        }

        cout << "Explanation: " << q.explanation << "\n";
    }

    cout << "\nFinal Score: " << score << "/10\n";
}
