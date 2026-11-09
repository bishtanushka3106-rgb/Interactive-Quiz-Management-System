# DSA Interactive Quiz: website

Your C++ quiz, now with a web frontend. The C++ program is the backend: it keeps
the questions in your `QuestionBank` linked list, checks answers, and serves the
website, so there is only one thing to run.

## Run it

```
cd backend
make
./quiz_server
```

Open http://localhost:8080

Needs a C++17 compiler (g++ or clang++) and make. No other installs: the two
libraries it uses (`httplib.h`, `json.hpp`) are included in `backend/`.

## What you get

- **Student**: pick a topic or all topics, answer up to 10 random questions
  (options shuffled, same as your console version), see the explanation after each
  answer, then a review. Correct answers stay on the server until a question is answered.
- **Admin** (`#/admin`): log in, list the question bank, search by topic,
  add multiple-choice or code-output questions, delete questions.
  Default password is `admin123`. Change it: `ADMIN_PASSWORD=yourpassword ./quiz_server`
- Admin changes are saved to `backend/data/questions.json`, so they survive restarts.
  Delete that file to go back to the built-in questions.

## Settings (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | 8080 | Port to listen on |
| `HOST` | 127.0.0.1 | Use `0.0.0.0` to let other devices on your network connect |
| `ADMIN_PASSWORD` | admin123 | Admin login password |
| `FRONTEND_DIR` | ../frontend | Where the website files are |
| `DATA_FILE` | data/questions.json | Where questions are saved |

## Files

```
backend/
  question_bank.h/.cpp   your linked list (unchanged)
  quiz.h/.cpp            your questions + console quiz (two small additions so the server can reuse them)
  main.cpp               your console menu (unchanged); build it with `make quiz_cli`
  server.cpp             new: the REST API + static file server
frontend/
  index.html, styles.css, app.js
```

## API

| Method and path | What it does |
|---|---|
| `GET /api/topics` | Topics with question counts |
| `POST /api/quiz/start` `{topic?}` | Starts a quiz, returns questions without answers |
| `POST /api/quiz/answer` `{sessionId, index, choice \| text}` | Checks one answer |
| `POST /api/admin/login` `{password}` | Returns a token (send as `Authorization: Bearer ...`) |
| `GET /api/admin/questions?topic=` | List with answers |
| `POST /api/admin/questions` | Add a question |
| `DELETE /api/admin/questions/:position` | Delete by position |
