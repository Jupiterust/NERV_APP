 nvic_irq_enable(SDIO_IRQn, 1, 0);                    // 使能SDIO中断，优先级为0
nvic_irq_enable(BSP_RS485_IRQn, 5, 0);
nvic_irq_enable(BSP_KEY_TIM_IRQn, 0, 0);
    nvic_irq_enable(EXTI0_IRQn, 2, 0);
    nvic_irq_enable(EXTI0_IRQn, 2, 0);
    nvic_irq_enable(EXTI1_IRQn, 2, 0);
    nvic_irq_enable(EXTI3_IRQn, 2, 0);
    nvic_irq_enable(EXTI4_IRQn, 2, 0);
    nvic_irq_enable(EXTI5_9_IRQn, 2, 0); // 共享中断向量
    nvic_irq_enable(BSP_KEY_TIM_IRQn, 0, 0);
*
这里是所有中断，有问题可能是中断优先级出现了问题，可以通过串口和led调试
*