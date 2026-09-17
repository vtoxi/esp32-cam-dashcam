import { HttpErrorResponse, HttpInterceptorFn } from '@angular/common/http';
import { inject } from '@angular/core';
import { catchError, throwError } from 'rxjs';
import { NotificationService } from '../services/notification.service';

/** Surfaces every failed API call as a toast, once, at the edge — no feature
 * component needs its own error-toast boilerplate. Components still catch
 * the re-thrown error where they need to react to it (e.g. keep a form open). */
export const errorInterceptor: HttpInterceptorFn = (req, next) => {
  const notifications = inject(NotificationService);
  return next(req).pipe(
    catchError((err: unknown) => {
      if (err instanceof HttpErrorResponse) {
        notifications.error(describeHttpError(err));
      }
      return throwError(() => err);
    }),
  );
};

function describeHttpError(err: HttpErrorResponse): string {
  if (err.status === 0) return 'Cannot reach the backend — check Settings > API Base URL.';
  if (err.status === 401) return 'Unauthorized — check the admin key in Settings.';
  if (err.status === 503 && typeof err.error === 'object' && err.error?.detail) {
    return err.error.detail as string;
  }
  if (err.status === 404) return 'Not found.';
  return `Request failed (${err.status}): ${err.statusText || 'unknown error'}`;
}
