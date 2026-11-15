#include "DamageInference.hpp"
#include "DamageEngine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace whm {
namespace {
std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::istringstream stream(s);
    std::string item;
    while (std::getline(stream, item, delim)) out.push_back(item);
    return out;
}
bool parseInt(const std::string &s, int &v) {
    if (s.empty()) return false;
    char *end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (*end || n < -100000 || n > 100000) return false;
    v = static_cast<int>(n);
    return true;
}
bool contains(const std::vector<int> &v, int value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}
std::vector<int> hitCounts(int full) {
    // Real streams land a handful of milestone counts; also cover the exact
    // hit totals seen in DamageTrace.log (9/15/16/18/19).
    std::vector<int> out;
    for (int k : {1, 2, 3, 4, 5, 6, 8, 9, 12, 15, 16, 18, full})
        if (k >= 1 && k <= full && !contains(out, k)) out.push_back(k);
    return out;
}
}

int DamageInference::truncMul(int value, double factor) {
    return static_cast<int>(std::floor(static_cast<double>(value) * factor + 1e-9));
}

double DamageInference::typeBonus(const MoveFingerprint &move, const InferenceState &state) {
    if (move.kind == 2) return state.spellMultiplier;
    if (move.kind == 1) return state.skillMultiplier;
    return 1.0;
}

double DamageInference::weatherBonus(const MoveFingerprint &move, const InferenceState &state) {
    if (state.typeIncludesWeather) return 1.0;
    if (move.kind == 2 && state.weather == 1) return 1.25;
    if (move.kind == 1 && state.weather == 4) return 1.25;
    return 1.0;
}

bool DamageInference::read(std::istream &in, std::vector<std::string> *errors) {
    moves_.clear();
    byCharacter_ = {};
    bool good = true;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto cells = split(line, '|');
        MoveFingerprint m;
        int kind = 0;
        bool valid = cells.size() == 9 &&
            parseInt(cells[0], m.character) && parseInt(cells[1], m.action) &&
            parseInt(cells[2], m.card) && parseInt(cells[5], m.neutralTotal) &&
            parseInt(cells[6], m.perHitBase) && parseInt(cells[7], m.hitCount) &&
            parseInt(cells[8], m.prorationPermille);
        if (valid) {
            if (cells[3] == "normal") kind = 0;
            else if (cells[3] == "skill") kind = 1;
            else if (cells[3] == "spell") kind = 2;
            else valid = false;
            m.kind = kind;
            m.notation = cells[4];
            valid = valid && m.character >= 0 && m.character < 20 &&
                m.action >= 300 && m.action < 690 && m.neutralTotal > 0 &&
                m.perHitBase > 0 && m.hitCount >= 1 && m.hitCount <= 64 &&
                m.prorationPermille >= 500 && m.prorationPermille <= 1000;
        }
        if (!valid) {
            good = false;
            if (errors) errors->push_back("bad hit profile at line " + std::to_string(number));
            continue;
        }
        moves_.push_back(std::move(m));
    }
    for (auto &m : moves_) m.calibration = calibration(m);
    for (const auto &m : moves_) byCharacter_[m.character].push_back(m);
    return good;
}

const std::vector<MoveFingerprint> &DamageInference::forCharacter(int character) const {
    static const std::vector<MoveFingerprint> empty;
    if (character < 0 || character >= 20) return empty;
    return byCharacter_[character];
}

int DamageInference::maxNeutralDamage(int character, int action) const {
    int best = 0;
    for (const auto &m : forCharacter(character))
        if (m.action == action && m.neutralTotal > best) best = m.neutralTotal;
    return best;
}

DamageInference::Simulation DamageInference::simulate(
    const MoveFingerprint &move, int hits, const InferenceState &state,
    double startRate, int startHP) const {
    Simulation out;
    out.endRate = startRate;
    out.endHP = startHP;
    if (move.perHitBase <= 0 || hits <= 0) return out;
    const double baseMul = state.attack * state.defense;
    if (!std::isfinite(baseMul) || baseMul <= 0.0 || baseMul > 50.0) return out;
    const double type = typeBonus(move, state);
    const double weather = weatherBonus(move, state);
    if (!std::isfinite(type) || type <= 0.0 || type > 50.0) return out;
    const double proration = static_cast<double>(move.prorationPermille) / 1000.0;
    double rate = startRate;
    int hp = startHP;
    int total = 0;
    for (int i = 0; i < hits; ++i) {
        int dealt = truncMul(move.perHitBase, baseMul);
        dealt = truncMul(dealt, rate);
        dealt = truncMul(dealt, type);
        dealt = truncMul(dealt, weather);
        dealt = truncMul(dealt, gutsFactor(hp));
        total += dealt;
        hp -= dealt;
        rate *= proration;
    }
    out.damage = total;
    out.endRate = rate;
    out.endHP = hp;
    return out;
}

double DamageInference::calibration(const MoveFingerprint &move) const {
    // Cleista's neutralTotal is a MEASURED full-hit run; our per-hit loop is
    // the ported formula. Calibrate once so a full neutral cast reproduces it.
    InferenceState neutral;
    neutral.character = move.character;
    neutral.defenderHP = 10000;
    neutral.comboRate = 1.0;
    const Simulation s = simulate(move, move.hitCount, neutral, 1.0, 10000);
    if (s.damage <= 0) return 1.0;
    return static_cast<double>(move.neutralTotal) / static_cast<double>(s.damage);
}

int DamageInference::expectedDamage(const MoveFingerprint &move, int hits,
                                    const InferenceState &state,
                                    double startRate, int startHP) const {
    const Simulation s = simulate(move, hits, state, startRate, startHP);
    return static_cast<int>(std::lround(static_cast<double>(s.damage) * move.calibration));
}

InferenceResult DamageInference::solve(int target, const InferenceState &state) const {
    InferenceResult result;
    result.target = target;
    if (target <= 0 || state.character < 0 || state.character >= 20) {
        result.reason = "invalid input";
        return result;
    }
    const auto &all = forCharacter(state.character);
    if (all.empty()) { result.reason = "no hit profiles"; return result; }

    std::vector<int> evidence;
    for (int a : state.originActions)
        if (a >= 300 && a < 690 && !contains(evidence, a)) evidence.push_back(a);
    if (state.actorAction >= 300 && state.actorAction < 690 &&
        !contains(evidence, state.actorAction))
        evidence.push_back(state.actorAction);

    const double tolerance = std::max(24.0, target * 0.04);
    double bestScore = 1e30;
    bool evidenceOnly = false;

    auto consider = [&](const std::vector<InferredPart> &parts,
                        const std::vector<int> &usedActions,
                        int expected, const std::string &reason) {
        const int residual = std::abs(target - expected);
        const bool paired = parts.size() > 1;
        const bool matched = std::all_of(usedActions.begin(), usedActions.end(),
            [&](int a) { return contains(evidence, a); });
        double score = static_cast<double>(residual);
        if (paired) score += std::max(30.0, target * 0.02); // prefer one cast
        if (!matched) score += target * 0.010;              // unexplained cast
        else score -= target * 0.015;                       // live evidence
        if (parts.size() == 1 && usedActions.front() == state.actorAction &&
            parts.front().hits == 1)
            score -= target * 0.010;                        // plain body hit
        if (score >= bestScore) return;
        bestScore = score;
        result.valid = true;
        result.expected = expected;
        result.residual = residual;
        result.parts = parts;
        result.reason = reason;
        evidenceOnly = matched;
        double confidence = 1.0 - static_cast<double>(residual) / std::max(target, 1);
        if (paired) confidence -= 0.12;
        if (!matched) confidence -= 0.10;
        result.confidence = std::clamp(confidence, 0.0, 1.0);
        int bestDamage = -1;
        for (const auto &p : parts) {
            if (p.damage <= bestDamage) continue;
            bestDamage = p.damage;
            result.primaryAction = p.action;
            result.primaryCard = p.card;
            result.primaryDamage = p.damage;
        }
    };

    // The game's combo counter gives BOTH a cumulative damage and a hit
    // count. Matching only the damage lets unrelated spells win by chance;
    // the hit count is what makes 5C's 18 bullets identifiable.
    auto hitsOk = [&](int hits) {
        return state.comboHits <= 0 || std::abs(hits - state.comboHits) <= 1;
    };
    auto singleRound = [&](const std::vector<const MoveFingerprint *> &pool) {
        for (const auto *m : pool) {
            // User's pruning rule: a cast whose maximum damage is below the
            // observed combo total cannot explain it on its own.
            const int maxSingle = expectedDamage(*m, m->hitCount, state, 1.0, 10000);
            if (static_cast<double>(maxSingle) + tolerance < target) continue;
            for (int k : hitCounts(m->hitCount)) {
                if (!hitsOk(k)) continue;
                const int expected = expectedDamage(*m, k, state,
                    state.comboRate, state.defenderHP);
                if (expected <= 0) continue;
                consider({InferredPart{m->action, m->card, expected, k}}, {m->action},
                         expected, "single " + m->notation + " x" + std::to_string(k));
            }
        }
    };
    auto pairRound = [&](std::vector<const MoveFingerprint *> pool) {
        if (pool.size() > 10) pool.resize(10);
        for (const auto *a : pool) {
            for (const auto *b : pool) {
                if (a == b) continue;
                for (int ka : hitCounts(a->hitCount)) {
                    const Simulation first = simulate(*a, ka, state,
                        state.comboRate, state.defenderHP);
                    const int firstDamage = static_cast<int>(std::lround(
                        static_cast<double>(first.damage) * a->calibration));
                    if (firstDamage <= 0) continue;
                    for (int kb : hitCounts(b->hitCount)) {
                        if (!hitsOk(ka + kb)) continue;
                        const Simulation second = simulate(*b, kb, state,
                            first.endRate, first.endHP);
                        const int secondDamage = static_cast<int>(std::lround(
                            static_cast<double>(second.damage) * b->calibration));
                        if (secondDamage <= 0) continue;
                        consider({InferredPart{a->action, a->card, firstDamage, ka},
                                  InferredPart{b->action, b->card, secondDamage, kb}},
                                 {a->action, b->action}, firstDamage + secondDamage,
                                 "pair " + a->notation + " x" + std::to_string(ka) +
                                 " + " + b->notation + " x" + std::to_string(kb));
                    }
                }
            }
        }
    };

    // Round A: only casts actually live in this combo. Body hits and owned
    // projectile streams are far more reliable than a full-table guess.
    std::vector<const MoveFingerprint *> evMoves, others;
    for (const auto &m : all) {
        if (contains(evidence, m.action)) evMoves.push_back(&m);
        else others.push_back(&m);
    }
    singleRound(evMoves);
    pairRound(evMoves);
    if (result.valid && result.residual <= tolerance && evidenceOnly) return result;

    // Round B: full table fallback, but score-penalised for lack of evidence.
    singleRound(others);
    std::vector<const MoveFingerprint *> heavy, streams;
    for (const auto &m : all) { heavy.push_back(&m); streams.push_back(&m); }
    std::sort(heavy.begin(), heavy.end(), [](const MoveFingerprint *a, const MoveFingerprint *b) {
        return a->neutralTotal > b->neutralTotal;
    });
    std::sort(streams.begin(), streams.end(), [](const MoveFingerprint *a, const MoveFingerprint *b) {
        return a->hitCount > b->hitCount;
    });
    // Multi-hit streams are essential for explaining 18/19-hit combos even
    // when no projectile owner was captured; heavy spells cover the rest.
    std::vector<const MoveFingerprint *> fallback;
    auto appendUnique = [&](const MoveFingerprint *m) {
        if (std::find(fallback.begin(), fallback.end(), m) == fallback.end())
            fallback.push_back(m);
    };
    for (std::size_t i = 0; i < heavy.size() && i < 5; ++i) appendUnique(heavy[i]);
    for (std::size_t i = 0; i < streams.size() && i < 5; ++i) appendUnique(streams[i]);
    for (const auto *m : evMoves) appendUnique(m);
    pairRound(fallback);

    if (!result.valid) { result.reason = "no hit profiles matched"; return result; }
    // A residual this large means the simulation did NOT explain the combo.
    // Never present a forced guess as a move name -- the HUD then falls back
    // to the authoritative COMBO DAMAGE label instead of inventing a cast.
    const double plausible = std::max(tolerance, static_cast<double>(target) * 0.06);
    if (result.residual > plausible) {
        result.valid = false;
        result.reason = "no plausible explanation (residual " +
                        std::to_string(result.residual) + ")";
        return result;
    }
    // When the game showed us which casts were live, a combination that uses
    // none of them is a numeric coincidence, not an inference. Require a
    // near-exact fit before offering it as a concrete move name.
    if (!evidence.empty() && !evidenceOnly && result.residual > tolerance) {
        result.valid = false;
        result.reason = "explanation without live evidence";
    }
    return result;
}

} // namespace whm
