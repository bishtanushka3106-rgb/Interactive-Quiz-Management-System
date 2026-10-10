
#include "question_bank.h"
#include <iostream>

QuestionBank::QuestionBank() {
    head = nullptr;
    count = 0;
}

QuestionBank::~QuestionBank() {
    Node* current = head;

    while (current != nullptr) {
        Node* nextNode = current->next;
        delete current;
        current = nextNode;
    }
}

void QuestionBank::addQuestion(const Question& q) {
    Node* newNode = new Node(q);

    if (head == nullptr) {
        head = newNode;
    } else {
        Node* current = head;

        while (current->next != nullptr) {
            current = current->next;
        }

        current->next = newNode;
    }

    count++;
}

bool QuestionBank::deleteQuestion(int position) {
    if (position < 1 || position > count) {
        return false;
    }

    Node* current = head;

    if (position == 1) {
        head = head->next;
        delete current;
        count--;
        return true;
    }

    for (int i = 1; i < position - 1; i++) {
        current = current->next;
    }

    Node* nodeToDelete = current->next;
    current->next = nodeToDelete->next;
    delete nodeToDelete;

    count--;
    return true;
}

void QuestionBank::displayQuestions() const {
    if (head == nullptr) {
        std::cout << "\nQuestion bank is empty.\n";
        return;
    }

    Node* current = head;
    int position = 1;

    std::cout << "\n--- DSA Question Bank ---\n";

    while (current != nullptr) {
        std::cout << position << ". ["
                  << current->data.topic << ", "
                  << current->data.difficulty << "] "
                  << current->data.text << "\n";

        current = current->next;
        position++;
    }

    std::cout << "Total questions: " << count << "\n";
}

bool QuestionBank::searchByTopic(
    const std::string& topic) const {

    Node* current = head;
    bool found = false;

    while (current != nullptr) {
        if (current->data.topic == topic) {
            std::cout << "\nFound: "
                      << current->data.text << "\n";
            found = true;
        }

        current = current->next;
    }

    if (!found) {
        std::cout << "\nNo questions found for topic: "
                  << topic << "\n";
    }

    return found;
}

std::vector<Question> QuestionBank::getAllQuestions() const {
    std::vector<Question> result;
    Node* current = head;

    while (current != nullptr) {
        result.push_back(current->data);
        current = current->next;
    }

    return result;
}

int QuestionBank::size() const {
    return count;
}
