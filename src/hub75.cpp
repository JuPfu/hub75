#include "hub75.hpp"

#include "hardware/irq.h"

// DMA_IRQ_0 / DMA_IRQ_1 are single hardware vectors for the whole MCU - irq_set_exclusive_handler
// allows exactly one handler per IRQ line, so the two handlers are installed exactly once here
// (on the first Hub75Driver<Cfg> instance created, regardless of Cfg) and dispatch to every
// registered instance, letting multiple differently- or identically-configured Hub75Driver
// instances coexist.

Hub75DriverBase::~Hub75DriverBase()
{
    unregister_instance();
}

void Hub75DriverBase::register_instance()
{
    critical_section_enter_blocking(&s_instance_lock);

    if (s_instance_count >= MAX_INSTANCES)
    {
        critical_section_exit(&s_instance_lock);
        panic("Hub75Driver: too many instances (max %u)\n", static_cast<unsigned>(MAX_INSTANCES));
    }

    s_instances[s_instance_count++] = this;

    if (!s_irq_installed)
    {
        irq_set_exclusive_handler(DMA_IRQ_0, global_ctrl_irq_handler);
        irq_set_exclusive_handler(DMA_IRQ_1, global_bitplane_irq_handler);
        irq_set_enabled(DMA_IRQ_0, true);
        irq_set_enabled(DMA_IRQ_1, true);
        s_irq_installed = true;
    }

    critical_section_exit(&s_instance_lock);
}

void Hub75DriverBase::unregister_instance()
{
    critical_section_enter_blocking(&s_instance_lock);

    for (size_t i = 0; i < s_instance_count; ++i)
    {
        if (s_instances[i] == this)
        {
            s_instances[i] = s_instances[s_instance_count - 1];
            s_instances[--s_instance_count] = nullptr;
            break;
        }
    }

    critical_section_exit(&s_instance_lock);
}

// Snapshots instances/count under the lock, then dispatches outside it - keeps the cross-core
// spinlock held only long enough to copy a couple of pointers, not for the (longer) per-instance
// IRQ work, which would otherwise stall a concurrent register_instance()/unregister_instance()
// on the other core.
void Hub75DriverBase::global_ctrl_irq_handler()
{
    Hub75DriverBase *instances[MAX_INSTANCES];
    critical_section_enter_blocking(&s_instance_lock);
    size_t n = s_instance_count;
    for (size_t i = 0; i < n; ++i)
        instances[i] = s_instances[i];
    critical_section_exit(&s_instance_lock);

    for (size_t i = 0; i < n; ++i)
        instances[i]->handle_ctrl_irq();
}

void Hub75DriverBase::global_bitplane_irq_handler()
{
    Hub75DriverBase *instances[MAX_INSTANCES];
    critical_section_enter_blocking(&s_instance_lock);
    size_t n = s_instance_count;
    for (size_t i = 0; i < n; ++i)
        instances[i] = s_instances[i];
    critical_section_exit(&s_instance_lock);

    for (size_t i = 0; i < n; ++i)
        instances[i]->handle_bitplane_irq();
}
