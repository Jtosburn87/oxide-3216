#include "Scenes.h"

SceneMemory::SceneMemory (ParamSet& p) : params (p)
{
    fadeTime = &params.add ("scene.fade", "Scene fade", 0.0f, maxFadeSeconds, 0.0f);
}

bool SceneMemory::isSceneControl (const Param& p)
{
    return (p.scale == Param::Scale::linear || p.scale == Param::Scale::log)
        && ! p.id.startsWith ("tape.") && ! p.id.startsWith ("osc.") && ! p.id.startsWith ("scene.")
        && ! p.id.startsWith ("tempo.") && ! p.id.startsWith ("click.");   // the song's tempo is not a mix setting
}

bool SceneMemory::isStored (int s) const
{
    return juce::isPositiveAndBelow (s, numScenes) && scenes[(size_t) s].stored;
}

juce::String SceneMemory::getTitle (int s) const
{
    return isStored (s) ? scenes[(size_t) s].title : juce::String();
}

juce::Time SceneMemory::getStoredTime (int s) const
{
    return isStored (s) ? scenes[(size_t) s].when : juce::Time();
}

void SceneMemory::setTitle (int s, const juce::String& title)
{
    if (! isStored (s))
        return;
    const auto t = title.trim().isNotEmpty() ? title.trim() : "Scene " + number (s);
    if (t != scenes[(size_t) s].title)
    {
        scenes[(size_t) s].title = t;
        ++revision;
    }
}

void SceneMemory::store (int s, const juce::String& title)
{
    if (! juce::isPositiveAndBelow (s, numScenes))
        return;
    stopFade();
    auto& scene = scenes[(size_t) s];
    scene.values.clear();
    for (auto* p : params.all())
        if (isSceneControl (*p))
            scene.values[p->id] = p->get();
    scene.stored = true;
    scene.when = juce::Time::getCurrentTime();
    scene.title = title.trim().isNotEmpty() ? title.trim() : "Scene " + number (s);
    current = selected = s;
    ++revision;
}

bool SceneMemory::recall (int s)
{
    if (! isStored (s))
        return false;
    stopFade();
    const double seconds = fadeTime->get();
    for (const auto& [id, value] : scenes[(size_t) s].values)
        if (auto* p = params.find (id); p != nullptr && isSceneControl (*p))
        {
            if (seconds < 0.01)
                p->set (value);
            else if (p->get() != value)
                fading.push_back ({ p, p->toNormalised (p->get()), p->toNormalised (value), value, p->get() });
        }
    if (! fading.empty())
    {
        fadeStart = juce::Time::getMillisecondCounterHiRes();
        fadeLength = seconds * 1000.0;
        startTimerHz (60);
    }
    current = selected = s;
    ++revision;
    return true;
}

void SceneMemory::advanceFade (double nowMs)
{
    if (fading.empty())
        return;
    const auto t = (float) juce::jlimit (0.0, 1.0, (nowMs - fadeStart) / fadeLength);
    const float shape = t * t * (3.0f - 2.0f * t);   // eases in and out, like a hand on the faders
    for (auto& g : fading)
    {
        if (g.param == nullptr)
            continue;
        if (g.param->get() != g.lastSet)
        {
            g.param = nullptr;   // moved by hand during the fade: it's yours now
            continue;
        }
        g.param->set (t >= 1.0f ? g.target : g.param->fromNormalised (g.from + (g.to - g.from) * shape));
        g.lastSet = g.param->get();
    }
    if (t >= 1.0f)
        stopFade();
}

void SceneMemory::timerCallback()
{
    advanceFade (juce::Time::getMillisecondCounterHiRes());
}

void SceneMemory::stopFade()
{
    stopTimer();
    fading.clear();
}

void SceneMemory::select (int s)
{
    if (juce::isPositiveAndBelow (s, numScenes) && s != selected)
    {
        selected = s;
        ++revision;
    }
}

int SceneMemory::nextStored (int direction) const
{
    for (int k = 1; k <= numScenes; ++k)
    {
        const int s = ((selected + direction * k) % numScenes + numScenes) % numScenes;
        if (scenes[(size_t) s].stored)
            return s;
    }
    return -1;
}

bool SceneMemory::isEdited() const
{
    if (! isStored (current))
        return false;
    for (const auto& [id, value] : scenes[(size_t) current].values)
        if (auto* p = params.find (id); p != nullptr && p->get() != value)
            return true;
    return false;
}

void SceneMemory::clear()
{
    stopFade();
    for (auto& s : scenes)
        s = Scene();
    current = -1;
    selected = 0;
    ++revision;
}

juce::var SceneMemory::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("current", current);
    root->setProperty ("selected", selected);
    juce::Array<juce::var> list;
    for (int s = 0; s < numScenes; ++s)
    {
        const auto& scene = scenes[(size_t) s];
        if (! scene.stored)
            continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("number", s + 1);
        o->setProperty ("title", scene.title);
        o->setProperty ("stored", scene.when.toISO8601 (true));
        auto* values = new juce::DynamicObject();
        for (const auto& [id, value] : scene.values)
            values->setProperty (id, value);
        o->setProperty ("values", juce::var (values));
        list.add (juce::var (o));
    }
    root->setProperty ("scenes", list);
    return juce::var (root);
}

void SceneMemory::fromVar (const juce::var& v)
{
    clear();
    if (auto* list = v["scenes"].getArray())
        for (const auto& o : *list)
        {
            const int s = (int) o["number"] - 1;
            if (! juce::isPositiveAndBelow (s, numScenes))
                continue;
            auto& scene = scenes[(size_t) s];
            scene.stored = true;
            scene.title = o["title"].toString();
            scene.when = juce::Time::fromISO8601 (o["stored"].toString());
            if (auto* values = o["values"].getDynamicObject())
                for (const auto& prop : values->getProperties())
                    scene.values[prop.name.toString()] = (float) (double) prop.value;

            // Before 0.1.6 a strip's preamp gain knob was called Trim ("ch1.trim"); there was no trim after it.
            std::map<juce::String, float> renamed;
            for (const auto& [id, value] : scene.values)
                if (id.endsWith (".trim"))
                    if (const auto gainId = id.dropLastCharacters (5) + ".gain"; scene.values.count (gainId) == 0)
                        renamed[gainId] = value;
            for (const auto& [gainId, value] : renamed)
            {
                scene.values[gainId] = value;
                scene.values[gainId.dropLastCharacters (5) + ".trim"] = 0.0f;
            }
        }
    current = isStored ((int) v.getProperty ("current", -1)) ? (int) v["current"] : -1;
    selected = juce::jlimit (0, numScenes - 1, (int) v.getProperty ("selected", 0));
    ++revision;
}
