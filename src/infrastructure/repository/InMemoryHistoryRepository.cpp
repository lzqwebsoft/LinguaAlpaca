#include "InMemoryHistoryRepository.hpp"

namespace LinguaAlpaca::Infrastructure::Repository {

void InMemoryHistoryRepository::AddRecord(const Domain::Model::HistoryRecord& record) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.push_back(record);
}

std::vector<Domain::Model::HistoryRecord> InMemoryHistoryRepository::GetAllRecords() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records;
}

void InMemoryHistoryRepository::ClearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
}

} // namespace LinguaAlpaca::Infrastructure::Repository
