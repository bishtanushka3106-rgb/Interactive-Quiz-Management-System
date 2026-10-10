
#ifndef QUESTION_BANK_H
#define QUESTION_BANK_H

#include <string>
#include <vector>

struct Option {
    std::string text;
    bool correct;
};

struct Question {
    std::string topic;
    std::string difficulty;
    std::string text;
    std::vector<Option> options;
    std::string codeAnswer;
    std::string explanation;
    bool isCode;
};

class QuestionBank {
private:
    struct Node {
        Question data;
        Node* next;

        Node(const Question& q) : data(q), next(nullptr) {}
    };

    Node* head;
    int count;

public:
    QuestionBank();
    ~QuestionBank();

    QuestionBank(const QuestionBank&) = delete;
    QuestionBank& operator=(const QuestionBank&) = delete;

    void addQuestion(const Question& q);
    bool deleteQuestion(int position);
    void displayQuestions() const;
    bool searchByTopic(const std::string& topic) const;
    std::vector<Question> getAllQuestions() const;
    int size() const;
};

#endif
