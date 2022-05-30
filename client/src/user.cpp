#include "include/user.h"
#include <nlohmann/json.hpp>
#include "include/actCollectionsHistory.h"

User::User(BookRep *bookRep) : bookRep_(bookRep) {
}

void User::init(const std::string &username, const std::string &password) {
    httplib::Params params{{"name", username}, {"password", password}};
    auto res = client_.Post("/init-user", params);
    if (res->status != 200)  // TODO: Check if smth went wrong
        return;
    token_ = res->body;
    isAuthorized_ = true;
}

bool User::isAuthorized() const {
    return isAuthorized_;
}

void User::exit() {
    token_ = "";
    isAuthorized_ = false;
    bookRep_->clear();
    // TODO: drop table with  words
}

std::vector<Book> User::getLibraryBooks() {
    std::cout << "Getting Books..." << std::endl;
    auto res = client_.Post("/library");
    if (res->status != 200)
        throw std::runtime_error("Can't load library, error code: " +
                                 std::to_string(res->status));
    nlohmann::json params = nlohmann::json::parse(res->body);
    std::vector<Book> books;
    for (auto &p : params) {
        books.emplace_back(p["id"], p["name"], p["author"]);
    }
    std::cout << "Got " << params.size() << " books" << std::endl;
    return books;
}

int User::addBookToCollection(int bookId) {
    std::unique_ptr<sql::Statement> stmt(
        bookRep_->manager_.getConnection().createStatement());
    std::unique_ptr<sql::ResultSet> reqRes(stmt->executeQuery(
        "SELECT * FROM collection WHERE id=" + std::to_string(bookId)));

    if (reqRes->next()) {
        return 0;
    }

    std::cout << "Adding book to collection" << std::endl;
    httplib::Params params;
    params.emplace("token", token_);
    params.emplace("bookId", std::to_string(bookId));

    auto res = client_.Post("/add-book", params);

    if (res->status != 200)
        throw std::runtime_error("Can't add book, error code: " +
                                 std::to_string(res->status));

    nlohmann::json book = nlohmann::json::parse(res->body);
    bookRep_->addAndSaveBook(book["id"], book["name"], book["author"],
                             book["text"]);

    syncCollection();
    newActionInCollection("add", bookId);
    userRepLocal::newValue(userRepLocal::getValue() + 1);
    return 1;
}

void User::deleteCollectionBook(int bookId) {
    std::cout << "Deleting book from server";

    httplib::Params params;
    params.emplace("token", token_);
    params.emplace("bookId", std::to_string(bookId));

    auto res = client_.Post("/delete-book", params);

    if (res->status != 200) {
        throw std::runtime_error("Can't add book, error code: " +
                                 std::to_string(res->status));
    } else {
        syncCollection();
        newActionInCollection("delete", bookId);
        userRepLocal::newValue(userRepLocal::getValue() + 1);
    }
}

void User::newActionInCollection(std::string action, int bookId) {
    std::cout << "Deleting book from server";
    httplib::Params params;
    params.emplace("token", token_);
    params.emplace("action", action);
    params.emplace("bookId", std::to_string(bookId));
    auto res = client_.Post("/new-collections-action", params);
    if (res->status != 200) {
        throw std::runtime_error("Can't add action, error code: " +
                                 std::to_string(res->status));
    }
}

std::vector<ActCollectionsHistory> User::getNewActions(int startAt) {
    std::cout << "Try to get new actions from server";

    httplib::Params params;
    params.emplace("token", token_);
    params.emplace("startAt", std::to_string(startAt));

    auto res = client_.Post("/new-actions", params);
    if (res->status != 200)
        throw std::runtime_error("Can't load new actions, error code: " +
                                 std::to_string(res->status));

    nlohmann::json param = nlohmann::json::parse(res->body);

    std::vector<ActCollectionsHistory> books;
    for (auto &p : param) {
        books.push_back({p["type"], p["bookId"]});
    }
    return books;
}

void User::syncCollection() {
    std::vector<ActCollectionsHistory> vec =
        this->getNewActions(userRepLocal::getValue() + 1);
    for (auto action : vec) {
        std::cout << " action = " << action.type << std::endl;
        if (action.type == "delete") {
            bookRep_->deleteBookById(action.bookId);
        } else {
            this->addBookToCollection(action.bookId);
        }
    }
    userRepLocal::newValue(userRepLocal::getValue() + vec.size());
}