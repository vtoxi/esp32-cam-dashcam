import { Injectable, inject } from '@angular/core';
import { ToastService } from '../../shared/components/toast/toast.service';

/** Thin semantic wrapper over ToastService so feature code depends on
 * "notification", not the toast implementation detail. */
@Injectable({ providedIn: 'root' })
export class NotificationService {
  private readonly toasts = inject(ToastService);

  success(message: string): void {
    this.toasts.success(message);
  }
  error(message: string): void {
    this.toasts.error(message);
  }
  info(message: string): void {
    this.toasts.info(message);
  }
}
