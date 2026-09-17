import { Injectable, signal } from '@angular/core';

export type ToastKind = 'success' | 'error' | 'info';

export interface Toast {
  id: number;
  kind: ToastKind;
  message: string;
}

let nextId = 1;

/** Signal-based toast queue, rendered by ToastHostComponent (mounted once in
 * AppComponent) — the plain-Tailwind replacement for MatSnackBar. */
@Injectable({ providedIn: 'root' })
export class ToastService {
  readonly toasts = signal<Toast[]>([]);

  success(message: string): void {
    this.push('success', message, 4000);
  }
  error(message: string): void {
    this.push('error', message, 6000);
  }
  info(message: string): void {
    this.push('info', message, 4000);
  }

  dismiss(id: number): void {
    this.toasts.update((items) => items.filter((t) => t.id !== id));
  }

  private push(kind: ToastKind, message: string, durationMs: number): void {
    const toast: Toast = { id: nextId++, kind, message };
    this.toasts.update((items) => [...items, toast]);
    setTimeout(() => this.dismiss(toast.id), durationMs);
  }
}
