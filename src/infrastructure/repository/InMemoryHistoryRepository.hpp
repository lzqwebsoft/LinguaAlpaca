#pragma once
#include <vector>
#include <mutex>
#include "../../domain/repository/IHistoryRepository.hpp"

namespace LinguaAlpaca::Infrastructure::Repository {

class InMemoryHistoryRepository : public Domain::Repository::IHistoryRepository {
public:
    void AddRecord(const Domain::Model::HistoryRecord& record) override;
    std::vector<Domain::Model::HistoryRecord> GetAllRecords() const override;
    void ClearAll() override;

private:
    mutable std::mutex m_mutex;
    std::vector<Domain::Model::HistoryRecord> m_records;
};

} // namespace LinguaAlpaca::Infrastructure::Repository
