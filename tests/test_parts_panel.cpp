#include "PartsPanel.h"

#include <QApplication>
#include <cassert>
#include <cstdio>

static std::vector<FEPart> makeParts()
{
    FEPart shell;
    shell.name = "Shell";
    shell.nodeIds = {1, 2, 3};
    shell.elementIds = {10, 11};
    shell.visible = true;

    FEPart beam;
    beam.name = "Beam";
    beam.nodeIds = {3, 4};
    beam.elementIds = {20};
    beam.visible = false;

    FEPart bolt;
    bolt.name = "Bolt";
    bolt.nodeIds = {5, 6};
    bolt.elementIds = {30, 31};
    bolt.visible = true;

    return {shell, beam, bolt};
}

static std::vector<glm::vec3> makeColors()
{
    return {
        glm::vec3(0.8f, 0.2f, 0.2f),
        glm::vec3(0.2f, 0.8f, 0.2f),
        glm::vec3(0.2f, 0.2f, 0.8f),
    };
}

static void assertStates(const std::vector<std::pair<int, bool>>& states,
                         const std::vector<bool>& expected)
{
    assert(states.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        assert(states[i].first == static_cast<int>(i));
        assert(states[i].second == expected[i]);
    }
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    PartsPanel panel;
    int visibilitySignalCount = 0;
    std::vector<std::pair<int, bool>> visibilityEvents;
    QObject::connect(&panel, &PartsPanel::partVisibilityChanged,
                     [&](int partIndex, bool visible) {
        ++visibilitySignalCount;
        visibilityEvents.emplace_back(partIndex, visible);
    });

    panel.setParts("Assembly", makeParts(), makeColors());
    assertStates(panel.partVisibilityStates(), {true, false, true});
    assert(visibilitySignalCount == 0);

    panel.setPartVisibleByIndex(1, true);
    assertStates(panel.partVisibilityStates(), {true, true, true});
    assert(visibilitySignalCount == 1);
    assert((visibilityEvents.back() == std::pair<int, bool>{1, true}));

    panel.isolateParts({2});
    assertStates(panel.partVisibilityStates(), {false, false, true});
    assert(visibilitySignalCount == 4);

    panel.setAllPartsVisible(true, false);
    assertStates(panel.partVisibilityStates(), {true, true, true});
    assert(visibilitySignalCount == 4);

    printf("PartsPanel visibility state test passed.\\n");
    return 0;
}
