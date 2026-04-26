#pragma once
namespace subsystem2
{

    // Forward declaration to prevent circular dependency
    class Evidence;

    /**
     * @brief Interface for the Observer Design Pattern.
     * Classes that implement this will be notified when new evidence is secured.
     */
    class IEvidenceObserver
    {
    public:
        virtual ~IEvidenceObserver() = default;

        // Called automatically by the Evidence entity
        virtual void onEvidenceSecured(Evidence *ev) = 0;
    };

} // namespace subsystem2
