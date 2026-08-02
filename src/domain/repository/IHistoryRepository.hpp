#pragma once
#include <vector>
#include "../model/HistoryRecord.hpp"

namespace LinguaAlpaca::Domain::Repository {

class IHistoryRepository {
public:
    virtual ~IHistoryRepository() = default;

    virtual void AddRecord(const Model::HistoryRecord& record) = 0;
    virtual std::vector<Model::HistoryRecord> GetAllRecords() const = 0;
    virtual void ClearAll() = 0;
};

} // namespace LinguaAlpaca::Domain::Repository
